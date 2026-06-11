// DOCX Core Pipeline — main entry point.
// Parses a .docx file, builds frame tree, layouts content, renders to PNG.
//
// Usage:
//   docx_reader <input.docx> [output.png]          — render to PNG
//   docx_reader --dump <input.docx> [output_dir]    — dump document + layout XML

// stb implementations — must be in exactly one .cpp file
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include "document.h"
#include "docx_reader.h"
#include "frame.h"
#include "layout.h"
#include "renderer.h"
#include "font_engine.h"
#include "pdf_writer.h"

#include <iostream>
#include <fstream>
#include <string>
#include <chrono>

int main(int argc, char* argv[])
{
    if (argc < 2)
    {
        std::cerr << "Usage: docx_reader <input.docx> [output.png|output.pdf]" << std::endl;
        std::cerr << "       docx_reader --dump <input.docx> [output_dir]" << std::endl;
        return 1;
    }

    // Check for --dump mode
    bool dumpMode = false;
    bool pdfMode = false;
    std::string inputPath;
    std::string outputPath;

    if (std::string(argv[1]) == "--dump")
    {
        dumpMode = true;
        if (argc < 3)
        {
            std::cerr << "Usage: docx_reader --dump <input.docx> [output_dir]" << std::endl;
            return 1;
        }
        inputPath = argv[2];
        outputPath = (argc >= 4) ? argv[3] : ".";
    }
    else
    {
        inputPath = argv[1];
        outputPath = (argc >= 3) ? argv[2] : "output.png";
    }

    // Auto-detect PDF mode from output extension
    if (!dumpMode && outputPath.size() >= 4)
    {
        std::string ext = outputPath.substr(outputPath.size() - 4);
        if (ext == ".pdf" || ext == ".PDF")
        {
            pdfMode = true;
        }
    }

    std::cout << "=== DOCX Core Pipeline ===" << std::endl;
    std::cout << "Input:  " << inputPath << std::endl;
    if (dumpMode)
    {
        std::cout << "Mode:   dump" << std::endl;
        std::cout << "Output: " << outputPath << std::endl;
    }
    else if (pdfMode)
    {
        std::cout << "Mode:   pdf" << std::endl;
        std::cout << "Output: " << outputPath << std::endl;
    }
    else
    {
        std::cout << "Output: " << outputPath << std::endl;
    }

    // Stage 1: Parse DOCX
    std::cout << "\n[Stage 1] Parsing DOCX..." << std::endl;
    auto t0 = std::chrono::high_resolution_clock::now();

    docx::Document doc;
    docx::DocxReader reader;
    if (!reader.read(inputPath, doc))
    {
        std::cerr << "Failed to read DOCX file: " << inputPath << std::endl;
        return 1;
    }

    auto t1 = std::chrono::high_resolution_clock::now();
    std::cout << "  Paragraphs: " << doc.paragraphs.size() << std::endl;
    std::cout << "  Tables:     " << doc.tables.size() << std::endl;
    std::cout << "  Images:     " << doc.images.size() << std::endl;
    std::cout << "  Styles:     " << doc.styles.size() << std::endl;
    std::cout << "  Time: "
              << std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count() << "ms"
              << std::endl;

    // Stage 2: Build frame tree
    std::cout << "\n[Stage 2] Building frame tree..." << std::endl;

    docx::RootFrame root;
    docx::FrameBuilder::build(doc, root, 96.0f);

    auto t2 = std::chrono::high_resolution_clock::now();
    std::cout << "  Pages: " << root.pages.size() << std::endl;
    std::cout << "  Time: "
              << std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1).count() << "ms"
              << std::endl;

    // Stage 3: Layout
    std::cout << "\n[Stage 3] Layout and pagination..." << std::endl;

    docx::FontEngine fonts;
    docx::LayoutEngine layout;
    layout.layout(doc, root, fonts);

    auto t3 = std::chrono::high_resolution_clock::now();
    std::cout << "  Pages after layout: " << root.pages.size() << std::endl;
    for (size_t i = 0; i < root.pages.size(); i++)
    {
        auto& page = root.pages[i];
        std::cout << "  Page " << i << ": " << page.body.textFrames.size() << " text frames, "
                  << page.body.tableFrames.size() << " table frames, "
                  << page.body.imageFrames.size() << " image frames" << std::endl;
    }
    std::cout << "  Time: "
              << std::chrono::duration_cast<std::chrono::milliseconds>(t3 - t2).count() << "ms"
              << std::endl;

    // If dump mode, output XML and exit
    if (dumpMode)
    {
        std::cout << "\n[Dump] Writing XML dumps..." << std::endl;

        std::string docPath = outputPath + "/docx_nodes.xml";
        std::string layPath = outputPath + "/docx_layout.xml";

        {
            std::ofstream f(docPath);
            docx::dumpDocumentXml(doc, f);
            std::cout << "  Document: " << docPath << std::endl;
        }
        {
            std::ofstream f(layPath);
            docx::dumpLayoutXml(root, f);
            std::cout << "  Layout:   " << layPath << std::endl;
        }

        auto tEnd = std::chrono::high_resolution_clock::now();
        std::cout << "\n=== Total: "
                  << std::chrono::duration_cast<std::chrono::milliseconds>(tEnd - t0).count()
                  << "ms ===" << std::endl;
        return 0;
    }

    // Stage 4: Render & Output
    if (pdfMode)
    {
        std::cout << "\n[Stage 4] Rendering to PDF..." << std::endl;

        if (!docx::PdfWriter::write(root, fonts, outputPath))
        {
            std::cerr << "Failed to save PDF: " << outputPath << std::endl;
            return 1;
        }

        auto t4 = std::chrono::high_resolution_clock::now();
        std::cout << "  Saved: " << outputPath << std::endl;
        std::cout << "  Time: "
                  << std::chrono::duration_cast<std::chrono::milliseconds>(t4 - t3).count() << "ms"
                  << std::endl;

        std::cout << "\n=== Total: "
                  << std::chrono::duration_cast<std::chrono::milliseconds>(t4 - t0).count()
                  << "ms ===" << std::endl;
    }
    else
    {
        std::cout << "\n[Stage 4] Rendering..." << std::endl;

        docx::Renderer renderer;
        docx::Bitmap bmp = renderer.render(root, fonts);

        auto t4 = std::chrono::high_resolution_clock::now();
        std::cout << "  Bitmap: " << bmp.width << "x" << bmp.height << " pixels" << std::endl;
        std::cout << "  Time: "
                  << std::chrono::duration_cast<std::chrono::milliseconds>(t4 - t3).count() << "ms"
                  << std::endl;

        // Save PNG
        std::cout << "\n[Stage 5] Saving PNG..." << std::endl;
        if (!bmp.savePNG(outputPath))
        {
            std::cerr << "Failed to save PNG: " << outputPath << std::endl;
            return 1;
        }

        auto t5 = std::chrono::high_resolution_clock::now();
        std::cout << "  Saved: " << outputPath << std::endl;
        std::cout << "  Time: "
                  << std::chrono::duration_cast<std::chrono::milliseconds>(t5 - t4).count() << "ms"
                  << std::endl;

        std::cout << "\n=== Total: "
                  << std::chrono::duration_cast<std::chrono::milliseconds>(t5 - t0).count()
                  << "ms ===" << std::endl;
    }

    return 0;
}
