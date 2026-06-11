#include "xml_util.h"
#include <cstring>

namespace xml
{
// Helper: build pugixml xpath query with w: namespace
// pugixml supports namespace prefixes in xpath if registered
static pugi::xpath_node_set find_nodes(pugi::xml_node node, const char* ns_uri,
                                       const char* local_name)
{
    // Walk children manually since xpath namespace registration is cumbersome
    return {};
}

std::string xpath(const char* path) { return std::string(path); }

pugi::xml_node child(pugi::xml_node node, const char* ns_uri, const char* local_name)
{
    if (!node)
        return {};
    for (auto c = node.first_child(); c; c = c.next_sibling())
    {
        const char* name = c.name();
        // Find the colon separator
        const char* colon = strchr(name, ':');
        if (colon)
        {
            // Check local name after colon
            if (strcmp(colon + 1, local_name) == 0)
            {
                // Optionally check namespace URI via xmlns attribute
                return c;
            }
        }
        else if (strcmp(name, local_name) == 0)
        {
            return c;
        }
    }
    return {};
}

std::vector<pugi::xml_node> children(pugi::xml_node node, const char* ns_uri,
                                     const char* local_name)
{
    std::vector<pugi::xml_node> result;
    if (!node)
        return result;
    for (auto c = node.first_child(); c; c = c.next_sibling())
    {
        const char* name = c.name();
        const char* colon = strchr(name, ':');
        if (colon)
        {
            if (strcmp(colon + 1, local_name) == 0)
            {
                result.push_back(c);
            }
        }
        else if (strcmp(name, local_name) == 0)
        {
            result.push_back(c);
        }
    }
    return result;
}

std::string attr(pugi::xml_node node, const char* ns_uri, const char* local_name)
{
    if (!node)
        return {};
    for (auto a = node.first_attribute(); a; a = a.next_attribute())
    {
        const char* name = a.name();
        const char* colon = strchr(name, ':');
        if (colon)
        {
            if (strcmp(colon + 1, local_name) == 0)
            {
                return a.value();
            }
        }
        else if (strcmp(name, local_name) == 0)
        {
            return a.value();
        }
    }
    return {};
}

int attr_int(pugi::xml_node node, const char* ns_uri, const char* local_name, int default_val)
{
    std::string val = attr(node, ns_uri, local_name);
    if (val.empty())
        return default_val;
    try
    {
        return std::stoi(val);
    }
    catch (...)
    {
        return default_val;
    }
}

bool attr_bool(pugi::xml_node node, const char* ns_uri, const char* local_name)
{
    std::string val = attr(node, ns_uri, local_name);
    return val == "true" || val == "1" || val == "on";
}

std::string text(pugi::xml_node node)
{
    if (!node)
        return {};
    // Collect all text child nodes
    std::string result;
    for (auto child = node.first_child(); child; child = child.next_sibling())
    {
        if (child.type() == pugi::node_pcdata || child.type() == pugi::node_cdata)
        {
            result += child.value();
        }
    }
    return result;
}

std::string child_text(pugi::xml_node node, const char* ns_uri, const char* local_name)
{
    auto c = child(node, ns_uri, local_name);
    return text(c);
}

bool has_child(pugi::xml_node node, const char* ns_uri, const char* local_name)
{
    return child(node, ns_uri, local_name) != pugi::xml_node{};
}

pugi::xml_node first_child(pugi::xml_node node, const char* ns_uri, const char* local_name)
{
    return child(node, ns_uri, local_name);
}

} // namespace xml
