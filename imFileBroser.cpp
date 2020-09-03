#include "imFileBroser.h"
#include "imgui.h"
#include <filesystem>

namespace ImGui
{
struct FileBrowserContext
{
    bool                  isOpen = false;
    std::filesystem::path currentPath;
};

static FileBrowserContext sContext;

bool FileBrowser(const std::string& name, std::string& outPath, bool& open, ImGuiFileBrowserFlags flags)
{
    if (!open)
    {
        return false;
    }

    if (!sContext.isOpen)
    {
        OpenPopup(name.c_str());
        sContext.isOpen = true;
    }

    bool result = false;

    if (BeginPopupModal(name.c_str(), &open, ImGuiWindowFlags_NoCollapse))
    {
        if (Button("Select"))
        {
            result = true;
            open   = false;
        }

        EndPopup();
    }

    int escIdx = GetIO().KeyMap[ImGuiKey_Escape];
    if (open && escIdx >= 0 && IsKeyPressed(escIdx))
    {
        open = false;
    }

    if (!open)
    {
        CloseCurrentPopup();
        sContext.isOpen = false;
    }

    return result;
}

}  // ImGui namespace
