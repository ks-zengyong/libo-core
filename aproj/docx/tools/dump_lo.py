"""
Dump LibreOffice's SwNodes and layout tree for a docx file.

Usage:
  cd c:/Users/A/lo/libo-core
  instdir/program/python.exe aproj/docx/tools/dump_lo.py "aproj/WPS Docs Quick Start Guide.docx"

Output:
  aproj/docx/tools/lo_nodes.xml   — SwNodes dump
  aproj/docx/tools/lo_layout.xml  — layout tree dump
"""

import sys
import os
import subprocess
import time

def main():
    if len(sys.argv) < 2:
        print("Usage: dump_lo.py <input.docx>")
        sys.exit(1)

    input_docx = os.path.abspath(sys.argv[1])
    if not os.path.exists(input_docx):
        print(f"File not found: {input_docx}")
        sys.exit(1)

    output_dir = os.path.dirname(os.path.abspath(__file__))
    nodes_xml = os.path.join(output_dir, "lo_nodes.xml")
    layout_xml = os.path.join(output_dir, "lo_layout.xml")

    soffice_dir = os.path.join(os.path.dirname(os.path.dirname(output_dir)),
                                "instdir", "program")
    soffice_exe = os.path.join(soffice_dir, "soffice.exe")

    if not os.path.exists(soffice_exe):
        print(f"soffice.exe not found at {soffice_exe}")
        sys.exit(1)

    # Write a Basic macro that opens the doc and dumps
    macro_content = r'''
Sub DumpDocument()
    Dim sURL As String
    sURL = ConvertToURL("{input_path}")

    ' Open the document
    Dim oDoc As Object
    oDoc = StarDesktop.loadComponentFromURL(sURL, "_blank", 0, Array())

    ' Wait for document to load and layout
    Wait 2000

    ' Get the document model
    Dim oTextDoc As Object
    oTextDoc = oDoc

    ' Force layout
    Dim oFrame As Object
    oFrame = ThisComponent.CurrentController.Frame

    ' Use XLayoutDumpFilter to dump layout
    ' We'll use a different approach: call dumpAsXml via dispatch

    ' For nodes dump: we need to access the internal SwDoc
    ' This is tricky via UNO - let's use the LayoutDump filter approach

    ' Save as LayoutDump format
    Dim oFilter As String
    oFilter = "LayoutDump"

    ' Get output stream for layout
    Dim sLayoutURL As String
    sLayoutURL = ConvertToURL("{layout_path}")

    ' Use export filter
    Dim args(0) As New com.sun.star.beans.PropertyValue
    args(0).Name = "FilterName"
    args(0).Value = "LayoutDump"

    ' This approach doesn't work directly - let's use the dispatch approach

    ' Close the document
    oDoc.close(True)
End Sub
'''.replace("{input_path}", input_docx.replace("\\", "\\\\")).replace(
        "{layout_path}", layout_xml.replace("\\", "\\\\"))

    # Actually, let's try a simpler approach using the command line
    # soffice --convert-to will use the LayoutDump filter if registered

    # First, let's try using the Python-UNO bridge directly
    # We need to start soffice in listening mode, then connect via UNO

    print(f"Input: {input_docx}")
    print(f"Output dir: {output_dir}")

    # Start soffice in listening mode
    print("Starting LibreOffice in listening mode...")
    lo_proc = subprocess.Popen(
        [soffice_exe, "--accept=socket,host=localhost,port=2002;urp;StarOffice.ServiceManager",
         "--norestore", "--nologo", "--headless"],
        cwd=os.path.dirname(soffice_exe),
        stdout=subprocess.PIPE, stderr=subprocess.PIPE
    )

    # Wait for soffice to start
    time.sleep(5)

    # Now connect via UNO and dump
    try:
        # Add LibreOffice program dir to Python path for UNO
        sys.path.insert(0, soffice_dir)
        sys.path.insert(0, os.path.join(soffice_dir, "python-core-3.10.14", "lib"))

        import uno
        from com.sun.star.beans import PropertyValue

        localContext = uno.getComponentContext()
        resolver = localContext.ServiceManager.createInstanceWithContext(
            "com.sun.star.bridge.UnoUrlResolver", localContext)

        ctx = resolver.resolve(
            "uno:socket,host=localhost,port=2002;urp;StarOffice.ComponentContext")
        smgr = ctx.ServiceManager
        desktop = smgr.createInstanceWithContext("com.sun.star.frame.Desktop", ctx)

        # Open the document
        url = "file:///" + input_docx.replace("\\", "/")
        doc = desktop.loadComponentFromURL(url, "_blank", 0, ())

        if doc is None:
            print("Failed to open document")
            return

        print("Document opened, waiting for layout...")
        time.sleep(3)

        # Force layout recalculation
        controller = doc.getCurrentController()
        frame = controller.getFrame()

        # Get the layout dump via export filter
        # The LayoutDump filter writes to XOutputStream
        from com.sun.star.io import XOutputStream

        # Create output stream for layout
        layout_file = os.path.join(output_dir, "lo_layout.xml")

        # Use dispatch to export
        # Actually, let's use a simpler approach: call the dump via dispatch command
        # The ".uno:ExportTo" with LayoutDump filter

        # Alternative: use XStorable with LayoutDump filter
        layout_url = "file:///" + layout_file.replace("\\", "/")

        # Store as layout dump
        args = ()
        try:
            # Try storing with LayoutDump filter
            props = []
            p1 = PropertyValue()
            p1.Name = "FilterName"
            p1.Value = "LayoutDump"
            props.append(p1)

            # This requires the filter to be registered
            # Let's try a different approach

            print("Attempting layout dump via filter...")

            # Get SwXTextDocument tunnel
            from com.sun.star.uno import XInterface
            import uno as uno_mod

            # Access internal model
            # The SwDoc is accessible via XUnoTunnel
            # This is complex via UNO...

            # Simplest approach: use the document's built-in dump
            # via the .uno:DebugDump command

            # Let's try dispatch
            smgr2 = ctx.ServiceManager
            dispatchHelper = smgr2.createInstanceWithContext(
                "com.sun.star.frame.DispatchHelper", ctx)

            # This might not work but worth trying
            # dispatchHelper.executeDispatch(frame, ".uno:DebugDump", "", 0, ())

            print("Note: Direct UNO dump is complex.")
            print("Falling back to SW_DEBUG approach...")

        except Exception as e:
            print(f"Export failed: {e}")

        # Close document
        doc.close(True)

    except Exception as e:
        print(f"UNO connection failed: {e}")
        print("Falling back to SW_DEBUG approach...")

    finally:
        # Kill soffice
        lo_proc.terminate()
        try:
            lo_proc.wait(timeout=5)
        except:
            lo_proc.kill()

    # Fallback: use SW_DEBUG approach
    print("\nTrying SW_DEBUG approach...")
    print("Set SW_DEBUG=1, open doc, press F12 (layout) / Shift-F12 (nodes)")

    env = os.environ.copy()
    env["SW_DEBUG"] = "1"

    # Start soffice with the document
    print(f"Opening {input_docx} with SW_DEBUG=1...")
    print("Press F12 in the window to dump layout.xml")
    print("Press Shift-F12 to dump nodes.xml")
    print("Then close the window.")

    proc = subprocess.Popen(
        [soffice_exe, input_docx],
        cwd=output_dir,
        env=env
    )

    print(f"Waiting for user to press F12/Shift-F12 and close...")
    proc.wait()

    # Check if dumps were created
    if os.path.exists(os.path.join(output_dir, "layout.xml")):
        print(f"layout.xml found in {output_dir}")
    if os.path.exists(os.path.join(output_dir, "nodes.xml")):
        print(f"nodes.xml found in {output_dir}")


if __name__ == "__main__":
    main()
