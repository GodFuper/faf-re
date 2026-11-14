-- Engine hook (data/fafengine, mounted by the engine at /fafengine and registered
-- as a hook directory): appended to /lua/system/Localization.lua.

local languageChangeListeners = {}

function AddLanguageChangeListener(fn)
    table.insert(languageChangeListeners, fn)
end

function RemoveLanguageChangeListener(fn)
    for k, v in languageChangeListeners do
        if v == fn then
            table.remove(languageChangeListeners, k)
            break
        end
    end
end

local _oldLOC = LOC
local _lastLocKey = nil

function LOC(str)
    local result = _oldLOC(str)
    if type(str) == 'string' and string.sub(str, 1, 5) == '<LOC ' then
        _lastLocKey = str
    end
    return result
end

function GetLastLocKey()
    return _lastLocKey
end

function ClearLastLocKey()
    _lastLocKey = nil
end

local old_language = language
function language(la)
    local oldLanguage = __language
    old_language(la)
    if oldLanguage ~= __language then
        for _, fn in languageChangeListeners do
            pcall(fn, __language)
        end
    end
end
