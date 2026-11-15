--Supreme King's Dark Fusion (YEdo special skill - "Evil HERO" Structure Deck)
local s,id=GetID()
local function used(tp,n) return Duel.GetFlagEffect(tp,id+n*1000000)>0 end
local function use(tp,n) Duel.RegisterFlagEffect(tp,id+n*1000000,0,0,0) end
local function flip(tp) Duel.Hint(HINT_SKILL_FLIP,tp,id|(1<<32)) Duel.Hint(HINT_CARD,tp,id) end
local function mz(tp) return Duel.GetLocationCount(tp,LOCATION_MZONE)>0 end
local function tohand(g,tp) if #g>0 then Duel.SendtoHand(g,nil,REASON_EFFECT) Duel.ConfirmCards(1-tp,g) end end
local function f1(c) return c:IsCode(94820406,12071500) and c:IsAbleToHand() end
function s.con1(e,tp) return Duel.IsExistingMatchingCard(f1,tp,LOCATION_DECK+LOCATION_GRAVE,0,1,nil) end
function s.op1(e,tp)
	Duel.Hint(HINT_SELECTMSG,tp,HINTMSG_ATOHAND)
	tohand(Duel.SelectMatchingCard(tp,aux.NecroValleyFilter(f1),tp,LOCATION_DECK+LOCATION_GRAVE,0,1,1,nil),tp)
end
local function f2(c,e,tp) return c:IsSetCard(0x6008) and c:IsType(TYPE_FUSION) and Duel.GetLocationCountFromEx(tp,tp,nil,c)>0 and c:IsCanBeSpecialSummoned(e,SUMMON_TYPE_FUSION,tp,true,false) end
function s.con2(e,tp) return Duel.IsExistingMatchingCard(f2,tp,LOCATION_EXTRA,0,1,nil,e,tp) end
function s.op2(e,tp)
	Duel.Hint(HINT_SELECTMSG,tp,HINTMSG_SPSUMMON)
	local tc=Duel.SelectMatchingCard(tp,f2,tp,LOCATION_EXTRA,0,1,1,nil,e,tp):GetFirst()
	if tc and Duel.SpecialSummon(tc,SUMMON_TYPE_FUSION,tp,tp,true,false,POS_FACEUP)>0 then
		tc:CompleteProcedure()
		local e1=Effect.CreateEffect(e:GetHandler())
		e1:SetType(EFFECT_TYPE_SINGLE)
		e1:SetCode(EFFECT_CANNOT_BE_EFFECT_TARGET)
		e1:SetProperty(EFFECT_FLAG_SINGLE_RANGE)
		e1:SetRange(LOCATION_MZONE)
		e1:SetValue(aux.tgoval)
		e1:SetReset(RESET_EVENT|RESETS_STANDARD|RESET_PHASE|PHASE_END)
		tc:RegisterEffect(e1)
	end
end
function s.initial_effect(c)
	aux.AddSkillProcedure(c,1,false,s.flipcon,s.flipop)
end
local function b1(e,tp) return not used(tp,1) and s.con1(e,tp) end
local function b2(e,tp) return not used(tp,2) and s.con2(e,tp) end
function s.flipcon(e,tp) return aux.CanActivateSkill(tp) and (b1(e,tp) or b2(e,tp)) end
function s.flipop(e,tp)
	flip(tp)
	local op=Duel.SelectEffect(tp,{b1(e,tp),aux.Stringid(id,0)},{b2(e,tp),aux.Stringid(id,1)})
	if op==1 then use(tp,1) s.op1(e,tp)
	elseif op==2 then use(tp,2) s.op2(e,tp) end
end
