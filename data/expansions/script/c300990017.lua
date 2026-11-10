--Soul Revival (story mod skill)
local s,id=GetID()
local function used(tp,n) return Duel.GetFlagEffect(tp,id+n*1000000)>0 end
local function use(tp,n) Duel.RegisterFlagEffect(tp,id+n*1000000,0,0,0) end
local function flip(tp) Duel.Hint(HINT_SKILL_FLIP,tp,id|(1<<32)) Duel.Hint(HINT_CARD,tp,id) end
local function free(tp,n) return Duel.GetLocationCount(tp,LOCATION_MZONE)>=(n or 1) end
--Special Summon a card from outside of the Duel
local function outside(tp,code,pos)
	if not free(tp) then return nil end
	local tc=Duel.CreateToken(tp,code)
	if Duel.SpecialSummon(tc,0,tp,tp,true,false,pos or POS_FACEUP_ATTACK)>0 then return tc end
	return nil
end
--put cards from outside of the Duel in the Extra Deck
local function addextra(tp,codes)
	local g=Group.CreateGroup()
	for _,code in ipairs(codes) do g:AddCard(Duel.CreateToken(tp,code)) end
	Duel.SendtoDeck(g,tp,SEQ_DECKTOP,REASON_RULE)
end
local function exfilter(tp,codes)
	return function(c) return c:IsCode(table.unpack(codes)) and Duel.GetLocationCountFromEx(tp,tp,nil,c)>0 end
end
local function hasextra(tp,codes) return Duel.IsExistingMatchingCard(exfilter(tp,codes),tp,LOCATION_EXTRA,0,1,nil) end
--Special Summon from the Extra Deck ignoring its Summoning conditions (treated as a proper Summon)
local function fromextra(tp,codes,sumtype)
	Duel.Hint(HINT_SELECTMSG,tp,HINTMSG_SPSUMMON)
	local tc=Duel.SelectMatchingCard(tp,exfilter(tp,codes),tp,LOCATION_EXTRA,0,1,1,nil):GetFirst()
	if tc and Duel.SpecialSummon(tc,sumtype,tp,tp,true,false,POS_FACEUP_ATTACK)>0 then
		tc:CompleteProcedure()
		return tc
	end
	return nil
end
local function startup(c,op)
	local e1=Effect.CreateEffect(c)
	e1:SetProperty(EFFECT_FLAG_UNCOPYABLE+EFFECT_FLAG_CANNOT_DISABLE)
	e1:SetType(EFFECT_TYPE_FIELD+EFFECT_TYPE_CONTINUOUS)
	e1:SetCode(EVENT_STARTUP)
	e1:SetCountLimit(1)
	e1:SetRange(0x5f)
	e1:SetOperation(op)
	c:RegisterEffect(e1)
end
local function oppmon(tp) return Duel.GetFieldGroupCount(tp,0,LOCATION_MZONE)>0 end

function s.initial_effect(c)
	aux.AddSkillProcedure(c,1,false,s.flipcon,s.flipop)
end
local function spfilter(c,e,tp) return c:IsMonster() and c:IsCanBeSpecialSummoned(e,0,tp,false,false) end
function s.flipcon(e,tp)
	return aux.CanActivateSkill(tp) and not used(tp,1) and free(tp)
		and Duel.IsExistingMatchingCard(spfilter,tp,LOCATION_GRAVE,LOCATION_GRAVE,1,nil,e,tp)
end
function s.flipop(e,tp)
	flip(tp)
	use(tp,1)
	Duel.Hint(HINT_SELECTMSG,tp,HINTMSG_SPSUMMON)
	local g=Duel.SelectMatchingCard(tp,spfilter,tp,LOCATION_GRAVE,LOCATION_GRAVE,1,1,nil,e,tp)
	if #g>0 then Duel.SpecialSummon(g,0,tp,tp,false,false,POS_FACEUP_ATTACK) end
end
