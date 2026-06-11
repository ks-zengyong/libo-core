#pragma once
// Simple PDF writer — outputs a minimal PDF from a Bitmap.
// Used for visual comparison with LibreOffice's PDF output.

#include "renderer.h"
#include <string>
#include <cstdint>
#include <vector>
#include <fstream>

namespace docx
{
class PdfWriter
{
public:
    // Write a bitmap to a PDF file.
    // Each page in the RootFrame becomes a PDF page.
    static bool write(const RootFrame& root, FontEngine& fonts, const std::string& path);

private:
    // PDF objects
    struct PdfObj
    {
        int id;
        std::string data;
        int offset = 0; // file offset (filled during write)
    };

    static void writeHeader(std::ostream& out);
    static int writePage(std::ostream& out, int objId, int contentObjId, int width, int height);
    static int writePageContent(std::ostream& out, int objId, const uint8_t* pixels, int width,
                                int height, int stride);
    static int writeCatalog(std::ostream& out, int objId, int pagesObjId);
    static int writePages(std::ostream& out, int objId, const std::vector<int>& pageObjIds);
    static void writeXref(std::ostream& out, const std::vector<int>& offsets, int rootObjId);
    static void writeTrailer(std::ostream& out, int rootObjId, int objCount);

    // Image encoding
    static std::string encodeRGB(const uint8_t* pixels, int width, int height, int stride);
};

} // namespace docx
