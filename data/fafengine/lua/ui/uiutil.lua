-- Engine hook (data/fafengine, mounted by the engine at /fafengine and registered
-- as a hook directory): appended to /lua/ui/uiutil.lua.

local _oldCreateText = CreateText
function CreateText(parent, label, pointSize, font, dropshadow)
    local rawLabel = label
    local text = _oldCreateText(parent, label, pointSize, font, dropshadow)
    if text and type(rawLabel) == 'string' and string.sub(rawLabel, 1, 5) == '<LOC ' then
        text._locKey = rawLabel
    end
    return text
end
