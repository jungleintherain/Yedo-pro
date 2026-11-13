--Blue Abyss Awakening (YEdo special skill - Blue-Eyes 2025 Structure Deck)
local s,id=GetID()
local function used(tp,n) return Duel.GetFlagEffect(tp,id+n*1000000)>0 end
local function use(tp,n) Duel.RegisterFlagEffect(tp,id+n*1000000,0,0,0) end
local function flip(tp) Duel.Hint(HINT_SKILL_FLIP,tp,id|(1<<32)) Duel.Hint(HINT_CARD,tp,id) end

--(1) Ritual Monster in the Deck
local function ritfilter(c,e,tp)
	return c:IsMonster() and c:IsType(TYPE_RITUAL) and c:IsCanBeSpecialSummoned(e,SUMMON_TYPE_RITUAL,tp,true,false)
end
--(2) Level 8 or higher Fusion/Synchro Monster in the Extra Deck
local function extype(c)
	if c:IsType(TYPE_FUSION) then return SUMMON_TYPE_FUSION end
	if c:IsType(TYPE_SYNCHRO) then return SUMMON_TYPE_SYNCHRO end
	return nil
end
local function exfilter(c,e,tp)
	local st=extype(c)
	if not st then return false end
	return c:IsLevelAbove(8) and Duel.GetLocationCountFromEx(tp,tp,nil,c)>0 and c:IsCanBeSpecialSummoned(e,st,tp,true,false)
end

--banish the monster during the End Phase of turn number "turn"
local function banish_at(e,tp,tc,turn)
	local fid=e:GetHandler():GetFieldID()
	tc:RegisterFlagEffect(id,RESET_EVENT|RESETS_STANDARD,EFFECT_FLAG_CLIENT_HINT,1,fid,aux.Stringid(id,2))
	local e1=Effect.CreateEffect(e:GetHandler())
	e1:SetType(EFFECT_TYPE_FIELD+EFFECT_TYPE_CONTINUOUS)
	e1:SetCode(EVENT_PHASE+PHASE_END)
	e1:SetProperty(EFFECT_FLAG_UNCOPYABLE+EFFECT_FLAG_CANNOT_DISABLE)
	e1:SetLabel(fid,turn)
	e1:SetLabelObject(tc)
	e1:SetCondition(function(ef)
		local f,t=ef:GetLabel()
		local c=ef:GetLabelObject()
		if c:GetFlagEffectLabel(id)~=f then
			ef:Reset()
			return false
		end
		return Duel.GetTurnCount()==t
	end)
	e1:SetOperation(function(ef)
		Duel.Hint(HINT_CARD,0,id)
		Duel.Remove(ef:GetLabelObject(),POS_FACEUP,REASON_EFFECT)
		ef:Reset()
	end)
	Duel.RegisterEffect(e1,tp)
end

function s.initial_effect(c)
	aux.AddSkillProcedure(c,1,false,s.flipcon,s.flipop)
end
local function b1(e,tp)
	return not used(tp,1) and Duel.GetLocationCount(tp,LOCATION_MZONE)>0
		and Duel.IsExistingMatchingCard(ritfilter,tp,LOCATION_DECK,0,1,nil,e,tp)
end
local function b2(e,tp)
	return not used(tp,2) and Duel.IsExistingMatchingCard(exfilter,tp,LOCATION_EXTRA,0,1,nil,e,tp)
end
function s.flipcon(e,tp) return aux.CanActivateSkill(tp) and (b1(e,tp) or b2(e,tp)) end
function s.flipop(e,tp)
	flip(tp)
	local op=Duel.SelectEffect(tp,{b1(e,tp),aux.Stringid(id,0)},{b2(e,tp),aux.Stringid(id,1)})
	if op==1 then
		use(tp,1)
		Duel.Hint(HINT_SELECTMSG,tp,HINTMSG_SPSUMMON)
		local tc=Duel.SelectMatchingCard(tp,ritfilter,tp,LOCATION_DECK,0,1,1,nil,e,tp):GetFirst()
		if tc and Duel.SpecialSummon(tc,SUMMON_TYPE_RITUAL,tp,tp,true,false,POS_FACEUP)>0 then
			tc:CompleteProcedure()
			--banished during the End Phase of the next turn
			banish_at(e,tp,tc,Duel.GetTurnCount()+1)
		end
	elseif op==2 then
		use(tp,2)
		Duel.Hint(HINT_SELECTMSG,tp,HINTMSG_SPSUMMON)
		local tc=Duel.SelectMatchingCard(tp,exfilter,tp,LOCATION_EXTRA,0,1,1,nil,e,tp):GetFirst()
		if tc and Duel.SpecialSummon(tc,extype(tc),tp,tp,true,false,POS_FACEUP)>0 then
			tc:CompleteProcedure()
			--banished during this turn's End Phase
			banish_at(e,tp,tc,Duel.GetTurnCount())
		end
	end
end
