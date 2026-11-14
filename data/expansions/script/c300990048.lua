--Eternal Love (YEdo special skill - "Yubel" Structure Deck)
local s,id=GetID()
s.sets={0x19d}
local NOCHECK=true
local function used(tp,n) return Duel.GetFlagEffect(tp,id+n*1000000)>0 end
local function use(tp,n) Duel.RegisterFlagEffect(tp,id+n*1000000,0,0,0) end
local function flip(tp) Duel.Hint(HINT_SKILL_FLIP,tp,id|(1<<32)) Duel.Hint(HINT_CARD,tp,id) end
local function isarch(c)
	for _,set in ipairs(s.sets) do
		if c:IsSetCard(set) then return true end
	end
	return false
end
--(1) add 1 card of the archetype from the Deck to the hand
local function thfilter(c)
	return isarch(c) and c:IsAbleToHand()
end
--(2) Special Summon 1 monster of the archetype from the GY / face-up banished
local function spfilter(c,e,tp)
	return isarch(c) and c:IsMonster() and (c:IsLocation(LOCATION_GRAVE) or c:IsFaceup())
		and c:IsCanBeSpecialSummoned(e,0,tp,NOCHECK,false)
		and (not c:IsType(TYPE_FUSION+TYPE_SYNCHRO+TYPE_XYZ+TYPE_LINK) or Duel.GetLocationCountFromEx(tp,tp,nil,c)>0)
		and (c:IsType(TYPE_FUSION+TYPE_SYNCHRO+TYPE_XYZ+TYPE_LINK) or Duel.GetLocationCount(tp,LOCATION_MZONE)>0)
end
function s.initial_effect(c)
	aux.AddSkillProcedure(c,1,false,s.flipcon,s.flipop)
end
local function b1(e,tp)
	return not used(tp,1) and Duel.IsExistingMatchingCard(thfilter,tp,LOCATION_DECK,0,1,nil)
end
local function b2(e,tp)
	return not used(tp,2) and Duel.IsExistingMatchingCard(spfilter,tp,LOCATION_GRAVE+LOCATION_REMOVED,0,1,nil,e,tp)
end
function s.flipcon(e,tp) return aux.CanActivateSkill(tp) and (b1(e,tp) or b2(e,tp)) end
function s.flipop(e,tp)
	flip(tp)
	local op=Duel.SelectEffect(tp,{b1(e,tp),aux.Stringid(id,0)},{b2(e,tp),aux.Stringid(id,1)})
	if op==1 then
		use(tp,1)
		Duel.Hint(HINT_SELECTMSG,tp,HINTMSG_ATOHAND)
		local g=Duel.SelectMatchingCard(tp,thfilter,tp,LOCATION_DECK,0,1,1,nil)
		if #g>0 then
			Duel.SendtoHand(g,nil,REASON_EFFECT)
			Duel.ConfirmCards(1-tp,g)
		end
	elseif op==2 then
		use(tp,2)
		Duel.Hint(HINT_SELECTMSG,tp,HINTMSG_SPSUMMON)
		local tc=Duel.SelectMatchingCard(tp,spfilter,tp,LOCATION_GRAVE+LOCATION_REMOVED,0,1,1,nil,e,tp):GetFirst()
		if tc and Duel.SpecialSummon(tc,0,tp,tp,NOCHECK,false,POS_FACEUP)>0 and NOCHECK then
			tc:CompleteProcedure()
		end
	end
end
