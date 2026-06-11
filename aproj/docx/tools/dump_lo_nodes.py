"""
Dump LibreOffice document nodes via UNO bridge.

Connects to a running soffice instance, opens a docx, and dumps nodes.xml.

Usage:
  1. Start soffice in listening mode:
     instdir/program/soffice.exe --accept=socket,host=localhost,port=2002;urp;StarOffice.ServiceManager --norestore --nologo --headless
  2. Run this script:
     instdir/program/python.exe aproj/docx/tools/dump_lo_nodes.py "aproj/WPS Docs Quick Start Guide.docx"
"""

import sys
import os
import subprocess
import time
import socket

def wait_for_port(port, timeout=30):
    """Wait for a port to become available."""
    start = time.time()
    while time.time() - start < timeout:
        try:
            s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            s.settimeout(1)
            s.connect(("localhost", port))
            s.close()
            return True
        except:
            time.sleep(0.5)
    return False

def kill_soffice():
    """Kill any running soffice processes."""
    try:
        subprocess.run(["taskkill", "/f", "/im", "soffice.exe"],
                       capture_output=True, timeout=5)
        time.sleep(2)
    except:
        pass

def main():
    if len(sys.argv) < 2:
        print("Usage: dump_lo_nodes.py <input.docx>")
        sys.exit(1)

    input_docx = os.path.abspath(sys.argv[1])
    if not os.path.exists(input_docx):
        print(f"File not found: {input_docx}")
        sys.exit(1)

    output_dir = os.path.dirname(os.path.abspath(__file__))
    nodes_xml = os.path.join(output_dir, "lo_nodes.xml")

    soffice_dir = os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(output_dir))),
                                "instdir", "program")
    soffice_exe = os.path.join(soffice_dir, "soffice.exe")
    python_exe = os.path.join(soffice_dir, "python.exe")

    if not os.path.exists(soffice_exe):
        print(f"soffice.exe not found at {soffice_exe}")
        sys.exit(1)

    # Kill any existing soffice
    print("Killing existing soffice processes...")
    kill_soffice()

    # Start soffice in listening mode
    print("Starting LibreOffice in listening mode...")
    lo_proc = subprocess.Popen(
        [soffice_exe,
         "--accept=socket,host=localhost,port=2002;urp;StarOffice.ServiceManager",
         "--norestore", "--nologo", "--headless"],
        cwd=os.path.dirname(soffice_exe),
        stdout=subprocess.PIPE, stderr=subprocess.PIPE
    )

    # Wait for soffice to start
    print("Waiting for soffice to start...")
    if not wait_for_port(2002, 30):
        print("Failed to start soffice")
        lo_proc.terminate()
        sys.exit(1)
    time.sleep(2)

    # Add LibreOffice program dir to Python path for UNO
    sys.path.insert(0, soffice_dir)

    try:
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
        url = "file:///" + input_docx.replace("\\", "/").replace(" ", "%20")
        print(f"Opening: {url}")

        doc = desktop.loadComponentFromURL(url, "_blank", 0, ())

        if doc is None:
            print("Failed to open document")
            return

        print("Document opened, waiting for layout...")
        time.sleep(3)

        # Force layout
        controller = doc.getCurrentController()
        if controller:
            # Try to get the frame and force layout
            frame = controller.getFrame()
            if frame:
                # Use dispatch to force layout recalc
                dispatchHelper = smgr.createInstanceWithContext(
                    "com.sun.star.frame.DispatchHelper", ctx)
                # This forces a full layout recalc
                dispatchHelper.executeDispatch(frame, ".uno:Zoom100Percent", "", 0, ())

        time.sleep(2)

        # Now dump the layout via the LayoutDump filter
        print("Dumping layout via LayoutDump filter...")
        layout_xml = os.path.join(output_dir, "lo_layout.xml")

        props = []
        p1 = PropertyValue()
        p1.Name = "FilterName"
        p1.Value = "writer_layout_dump"
        props.append(p1)

        p2 = PropertyValue()
        p2.Name = "Overwrite"
        p2.Value = True
        props.append(p2)

        layout_url = "file:///" + layout_xml.replace("\\", "/").replace(" ", "%20")
        try:
            doc.storeToURL(layout_url, tuple(props))
            print(f"Layout dump saved to: {layout_xml}")
        except Exception as e:
            print(f"Layout dump failed: {e}")

        # For nodes dump, we need to access the internal SwDoc
        # This requires XUnoTunnel
        print("Attempting nodes dump via XUnoTunnel...")

        # Get SwXTextDocument via XUnoTunnel
        from com.sun.star.lang import XUnoTunnel

        # Try to get the tunnel
        try:
            tunnel = doc.QueryInterface(uno.getTypeByName("com.sun.star.lang.XUnoTunnel"))
            if tunnel:
                # Get the SwDoc pointer
                swdoc_ptr = tunnel.getSomething(0)
                print(f"Got SwDoc pointer: {swdoc_ptr}")
            else:
                print("XUnoTunnel not available")
        except Exception as e:
            print(f"XUnoTunnel failed: {e}")

        # Close document
        doc.close(True)

    except Exception as e:
        print(f"Error: {e}")
        import traceback
        traceback.print_exc()

    finally:
        # Kill soffice
        print("Killing soffice...")
        lo_proc.terminate()
        try:
            lo_proc.wait(timeout=5)
        except:
            lo_proc.kill()
        time.sleep(1)

if __name__ == "__main__":
    main()
