-- Engine hook (data/fafengine, mounted by the engine at /fafengine and registered
-- as a hook directory): appended to /lua/maui/text.lua.

local liveTexts = setmetatable({}, { __mode = 'k' })

local _oldTextInit = Text.__init
Text.__init = function(self, parent, debugname)
    _oldTextInit(self, parent, debugname)
    liveTexts[self] = true
end

Text.SetText = function(self, text)
    if type(text) == 'string' then
        if string.sub(text, 1, 5) == '<LOC ' then
            self._locKey = text
            text = LOC(text)
        else
            local lastKey = GetLastLocKey and GetLastLocKey()
            if lastKey and LOC(lastKey) == text then
                self._locKey = lastKey
            elseif self._locKey and text ~= LOC(self._locKey) then
                self._locKey = nil
            end
        end
    end
    if ClearLastLocKey then
        ClearLastLocKey()
    end
    moho.text_methods.SetText(self, text)
end

local _oldTextOnDestroy = Text.OnDestroy
Text.OnDestroy = function(self)
    liveTexts[self] = nil
    if _oldTextOnDestroy then
        _oldTextOnDestroy(self)
    end
end

function UpdateAllTextLocalization()
    for textObj in liveTexts do
        if textObj._font and textObj._locKey then
            pcall(function()
                textObj:SetText(textObj._locKey)
            end)
        end
    end
end

if AddLanguageChangeListener then
    AddLanguageChangeListener(function(newLang)
        UpdateAllTextLocalization()
    end)
end
