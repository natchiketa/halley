local function update(time, families)
    local elems = families["mainFamily"].elems
    for i = 1, #elems do
        local e = elems[i]
    end
end

function makeUpdate(systemPtr, numFamilies)
    local families = {}
    local familiesList = {}
    
    for i = 1, numFamilies do
        local name = Halley.getFamilyName(systemPtr, i - 1)
        local fam = {}
        fam.idx = i
        fam.name = name
        fam.elems = {}

        families[name] = fam
        familiesList[i] = fam
    end
    
    return function(time)
        for i = 1, numFamilies do
            local elems = familiesList[i].elems
            local n = Halley.getFamilyCount(systemPtr, 0)
            
            for j = 1, n do
                if (elems[j] == nil) then
                    elems[j] = {}
                end
                Halley.getFamilyEntry(elems[j], systemPtr, i - 1, j - 1)
            end
        end
        
        update(time, families)
    end
end