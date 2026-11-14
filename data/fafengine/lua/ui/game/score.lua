-- Engine hook (data/fafengine, mounted by the engine at /fafengine and registered
-- as a hook directory): appended to /lua/ui/game/score.lua.

if AddLanguageChangeListener then
    AddLanguageChangeListener(function(newLang)
        if controls and controls.bg and not controls.bg:IsDestroyed() then
            controls.bg:Destroy()
            controls.bg = nil
            if controls.collapseArrow and not controls.collapseArrow:IsDestroyed() then
                controls.collapseArrow:Destroy()
                controls.collapseArrow = nil
            end
            CreateScoreUI()
        end
    end)
end
