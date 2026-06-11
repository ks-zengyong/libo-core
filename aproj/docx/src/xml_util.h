#pragma once
// XML utility helpers for OOXML namespace-aware access.
// Wraps pugixml with convenience functions for DOCX parsing.

#include <string>
#include <vector>
#include "pugixml.hpp"

namespace xml
{
// OOXML namespace URIs
namespace ns
{
constexpr const char* w = "http://schemas.openxmlformats.org/wordprocessingml/2006/main";
constexpr const char* wp = "http://schemas.openxmlformats.org/drawingml/2006/wordprocessingDrawing";
constexpr const char* a = "http://schemas.openxmlformats.org/drawingml/2006/main";
constexpr const char* r = "http://schemas.openxmlformats.org/officeDocument/2006/relationships";
constexpr const char* pic = "http://schemas.openxmlformats.org/drawingml/2006/picture";
constexpr const char* rel = "http://schemas.openxmlformats.org/package/2006/relationships";
constexpr const char* ct = "http://schemas.openxmlformats.org/package/2006/content-types";
constexpr const char* mc = "http://schemas.openxmlformats.org/markup-compatibility/2006";
constexpr const char* v = "urn:schemas-microsoft-com:vml";
constexpr const char* o = "urn:schemas-microsoft-com:office:office";
constexpr const char* wps = "http://www.wps.cn/officeDocument/2013/wpsCustomData";
} // namespace ns

// Build a namespace-aware xpath string like "w:p/w:r/w:t"
std::string xpath(const char* path);

// Get child element by local name with namespace prefix
pugi::xml_node child(pugi::xml_node node, const char* ns_uri, const char* local_name);

// Get all children with given namespace and local name
std::vector<pugi::xml_node> children(pugi::xml_node node, const char* ns_uri,
                                     const char* local_name);

// Get attribute value as string (empty if not found)
std::string attr(pugi::xml_node node, const char* ns_uri, const char* local_name);

// Get attribute value as int (default_val if not found)
int attr_int(pugi::xml_node node, const char* ns_uri, const char* local_name, int default_val = 0);

// Get attribute value as bool (false if not found)
bool attr_bool(pugi::xml_node node, const char* ns_uri, const char* local_name);

// Get text content of a node
std::string text(pugi::xml_node node);

// Get text of a child element (e.g., <w:t>text</w:t>)
std::string child_text(pugi::xml_node node, const char* ns_uri, const char* local_name);

// Check if a child element exists
bool has_child(pugi::xml_node node, const char* ns_uri, const char* local_name);

// Get first child element with given namespace+name
pugi::xml_node first_child(pugi::xml_node node, const char* ns_uri, const char* local_name);

} // namespace xml
