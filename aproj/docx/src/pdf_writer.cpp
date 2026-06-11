#include "pdf_writer.h"
#include <sstream>
#include <cstring>
#include <cmath>
#include "miniz.h"

namespace docx
{
// Compress data using miniz (FlateDecode)
static std::string flateEncode(const std::string& data)
{
    if (data.empty())
        return data;

    // Estimate compressed size
    mz_ulong bound = mz_compressBound(static_cast<mz_ulong>(data.size()));
    std::vector<unsigned char> outBuf(bound);

    int ret
        = mz_compress(outBuf.data(), &bound, reinterpret_cast<const unsigned char*>(data.data()),
                      static_cast<mz_ulong>(data.size()));
    if (ret != MZ_OK)
    {
        return data; // Return uncompressed on failure
    }
    return std::string(reinterpret_cast<char*>(outBuf.data()), bound);
}

bool PdfWriter::write(const RootFrame& root, FontEngine& fonts, const std::string& path)
{
    std::ofstream out(path, std::ios::binary);
    if (!out.is_open())
        return false;

    // Render all pages at once
    Renderer renderer;
    Bitmap fullBitmap = renderer.render(root, fonts);

    // Split into per-page bitmaps
    struct PageBitmap
    {
        std::vector<uint8_t> pixels;
        int width, height, stride;
    };
    std::vector<PageBitmap> pageBitmaps;

    int yOffset = 0;
    float gap = 20;
    for (auto& page : root.pages)
    {
        int pw = (int)ceilf(page.width);
        int ph = (int)ceilf(page.height);
        if (pw <= 0)
            pw = 794;
        if (ph <= 0)
            ph = 1123;

        PageBitmap pb;
        pb.width = pw;
        pb.height = ph;
        pb.stride = pw * 4;
        pb.pixels.resize(pb.stride * ph, 255); // white background

        // Copy pixels from full bitmap
        int srcX = (fullBitmap.width - pw) / 2; // centered
        for (int y = 0; y < ph && (yOffset + y) < fullBitmap.height; y++)
        {
            const uint8_t* srcRow = fullBitmap.pixels.data() + (yOffset + y) * fullBitmap.stride;
            uint8_t* dstRow = pb.pixels.data() + y * pb.stride;
            for (int x = 0; x < pw && (srcX + x) < fullBitmap.width; x++)
            {
                int sx = srcX + x;
                if (sx >= 0)
                {
                    dstRow[x * 4 + 0] = srcRow[sx * 4 + 0];
                    dstRow[x * 4 + 1] = srcRow[sx * 4 + 1];
                    dstRow[x * 4 + 2] = srcRow[sx * 4 + 2];
                    dstRow[x * 4 + 3] = srcRow[sx * 4 + 3];
                }
            }
        }

        pageBitmaps.push_back(std::move(pb));
        yOffset += ph + (int)gap;
    }

    if (pageBitmaps.empty())
        return false;

    // PDF header
    writeHeader(out);

    // Track object offsets for xref
    std::vector<int> offsets;
    int objId = 1;

    // Catalog
    int catalogObjId = objId++;
    int pagesObjId = objId++;
    std::vector<int> pageObjIds;
    std::vector<int> contentObjIds;

    // Reserve space for page and content objects
    for (size_t i = 0; i < pageBitmaps.size(); i++)
    {
        pageObjIds.push_back(objId++);
        contentObjIds.push_back(objId++);
    }

    // Write catalog
    offsets.push_back(static_cast<int>(out.tellp()));
    writeCatalog(out, catalogObjId, pagesObjId);

    // Write pages
    offsets.push_back(static_cast<int>(out.tellp()));
    writePages(out, pagesObjId, pageObjIds);

    // Write each page and its content
    for (size_t i = 0; i < pageBitmaps.size(); i++)
    {
        auto& pb = pageBitmaps[i];

        // Page object
        offsets.push_back(static_cast<int>(out.tellp()));
        writePage(out, pageObjIds[i], contentObjIds[i], pb.width, pb.height);

        // Content object (image)
        offsets.push_back(static_cast<int>(out.tellp()));
        writePageContent(out, contentObjIds[i], pb.pixels.data(), pb.width, pb.height, pb.stride);
    }

    // Xref and trailer
    writeXref(out, offsets, catalogObjId);

    out.close();
    return true;
}

void PdfWriter::writeHeader(std::ostream& out)
{
    out << "%PDF-1.4\n";
    out << "%\xe2\xe3\xcf\xd3\n"; // binary marker
}

int PdfWriter::writeCatalog(std::ostream& out, int objId, int pagesObjId)
{
    out << objId << " 0 obj\n";
    out << "<< /Type /Catalog /Pages " << pagesObjId << " 0 R >>\n";
    out << "endobj\n";
    return objId;
}

int PdfWriter::writePages(std::ostream& out, int objId, const std::vector<int>& pageObjIds)
{
    out << objId << " 0 obj\n";
    out << "<< /Type /Pages /Kids [";
    for (int id : pageObjIds)
    {
        out << " " << id << " 0 R";
    }
    out << " ] /Count " << pageObjIds.size() << " >>\n";
    out << "endobj\n";
    return objId;
}

int PdfWriter::writePage(std::ostream& out, int objId, int contentObjId, int width, int height)
{
    // PDF uses points (1/72 inch). At 96 DPI, 1 pixel = 72/96 points = 0.75 points
    float wPt = width * 72.0f / 96.0f;
    float hPt = height * 72.0f / 96.0f;

    out << objId << " 0 obj\n";
    out << "<< /Type /Page\n";
    out << "   /MediaBox [0 0 " << wPt << " " << hPt << "]\n";
    out << "   /Contents " << contentObjId << " 0 R\n";
    out << "   /Resources << >>\n";
    out << ">>\n";
    out << "endobj\n";
    return objId;
}

int PdfWriter::writePageContent(std::ostream& out, int objId, const uint8_t* pixels, int width,
                                int height, int stride)
{
    // Encode image as raw RGB
    std::string rgbData = encodeRGB(pixels, width, height, stride);
    std::string compressed = flateEncode(rgbData);

    // PDF content stream: draw the image
    std::ostringstream content;
    float wPt = width * 72.0f / 96.0f;
    float hPt = height * 72.0f / 96.0f;
    content << "q\n";
    content << wPt << " 0 0 " << hPt << " 0 0 cm\n";
    content << "/Im1 Do\n";
    content << "Q\n";
    std::string contentStr = content.str();

    // Image XObject
    out << objId << " 0 obj\n";
    out << "<< /Length " << contentStr.size() << "\n";
    out << "   /Filter /FlateDecode\n";
    out << ">>\n";
    out << "stream\n";
    out.write(contentStr.data(), contentStr.size());
    out << "\nendstream\n";
    out << "endobj\n";

    // Image object (separate object)
    int imgObjId = objId + 1000; // Use a high ID to avoid conflicts
    out << imgObjId << " 0 obj\n";
    out << "<< /Type /XObject /Subtype /Image\n";
    out << "   /Width " << width << " /Height " << height << "\n";
    out << "   /ColorSpace /DeviceRGB\n";
    out << "   /BitsPerComponent 8\n";
    out << "   /Filter /FlateDecode\n";
    out << "   /Length " << compressed.size() << "\n";
    out << ">>\n";
    out << "stream\n";
    out.write(compressed.data(), compressed.size());
    out << "\nendstream\n";
    out << "endobj\n";

    return objId;
}

void PdfWriter::writeXref(std::ostream& out, const std::vector<int>& offsets, int rootObjId)
{
    int xrefPos = static_cast<int>(out.tellp());
    int objCount = static_cast<int>(offsets.size()) + 1; // +1 for free entry

    out << "xref\n";
    out << "0 " << objCount << "\n";
    out << "0000000000 65535 f \n";
    for (int offset : offsets)
    {
        char buf[20];
        snprintf(buf, sizeof(buf), "%010d", offset);
        out << buf << " 00000 n \n";
    }

    out << "trailer\n";
    out << "<< /Size " << objCount << " /Root " << rootObjId << " 0 R >>\n";
    out << "startxref\n";
    out << xrefPos << "\n";
    out << "%%EOF\n";
}

std::string PdfWriter::encodeRGB(const uint8_t* pixels, int width, int height, int stride)
{
    std::string result;
    result.reserve(width * height * 3);
    for (int y = 0; y < height; y++)
    {
        const uint8_t* row = pixels + y * stride;
        for (int x = 0; x < width; x++)
        {
            // Input is RGBA, output is RGB
            result += static_cast<char>(row[x * 4 + 0]);
            result += static_cast<char>(row[x * 4 + 1]);
            result += static_cast<char>(row[x * 4 + 2]);
        }
    }
    return result;
}

} // namespace docx
