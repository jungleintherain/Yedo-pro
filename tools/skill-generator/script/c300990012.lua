--Blue-Eyes Supremacy (story mod skill)
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
	startup(c,function(e,tp) addextra(tp,{23995346}) end)
end
local function b1(tp) return not used(tp,1) and free(tp) end
local function b2(tp) return not used(tp,2) and oppmon(tp) and Duel.IsExistingMatchingCard(aux.FaceupFilter(Card.IsCode,89631139),tp,LOCATION_MZONE,0,1,nil) end
function s.flipcon(e,tp) return aux.CanActivateSkill(tp) and (b1(tp) or b2(tp)) end
function s.flipop(e,tp)
	flip(tp)
	local op=Duel.SelectEffect(tp,{b1(tp),aux.Stringid(id,0)},{b2(tp),aux.Stringid(id,1)})
	if op==1 then
		use(tp,1)
		outside(tp,89631139)
	elseif op==2 then
		use(tp,2)
		Duel.Destroy(Duel.GetFieldGroup(tp,0,LOCATION_MZONE),REASON_EFFECT)
		--only "Blue-Eyes White Dragon" can attack this turn
		local e1=Effect.CreateEffect(e:GetHandler())
		e1:SetType(EFFECT_TYPE_FIELD)
		e1:SetCode(EFFECT_CANNOT_ATTACK)
		e1:SetTargetRange(LOCATION_MZONE,0)
		e1:SetTarget(function(_,c) return not c:IsCode(89631139) end)
		e1:SetReset(RESET_PHASE|PHASE_END)
		Duel.RegisterEffect(e1,tp)
	end
end
