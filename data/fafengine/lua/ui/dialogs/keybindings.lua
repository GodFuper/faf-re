-- Engine hook (data/fafengine, mounted by the engine at /fafengine and registered
-- as a hook directory): appended to /lua/ui/dialogs/keybindings.lua.

local keyCategories = import("/lua/keymap/keycategories.lua").keyCategories
local lowerCategoryLookup = {}
for catKey, locStr in keyCategories do
    lowerCategoryLookup[string.lower(catKey)] = locStr
end

local _oldFormatData = FormatData
function FormatData()
    if keyGroups then
        for category, group in keyGroups do
            local locTag = lowerCategoryLookup[string.lower(category)]
            if locTag then
                group.text = LOC(locTag)
            end
        end
    end
    return _oldFormatData()
end
