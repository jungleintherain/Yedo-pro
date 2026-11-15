--Destiny Draw (YEdo EXPERT bot skill, automatic - "Destiny HERO" Structure Deck)
local s,id=GetID()
local function used(tp,n) return Duel.GetFlagEffect(tp,id+n*1000000)>0 end
local function use(tp,n) Duel.RegisterFlagEffect(tp,id+n*1000000,0,0,0) end
local function flip(tp) Duel.Hint(HINT_SKILL_FLIP,tp,id|(1<<32)) Duel.Hint(HINT_CARD,tp,id) end
local function mz(tp) return Duel.GetLocationCount(tp,LOCATION_MZONE)>0 end
local function tohand(g,tp) if #g>0 then Duel.SendtoHand(g,nil,REASON_EFFECT) Duel.ConfirmCards(1-tp,g) end end
local function f1(c,e,tp) return c:IsSetCard(0xc008) and c:IsMonster() and c:IsCanBeSpecialSummoned(e,0,tp,false,false) end
function s.con1(e,tp) return mz(tp) and Duel.IsExistingMatchingCard(f1,tp,LOCATION_GRAVE,0,1,nil,e,tp) end
function s.op1(e,tp)
	Duel.Hint(HINT_SELECTMSG,tp,HINTMSG_SPSUMMON)
	local g=Duel.SelectMatchingCard(tp,aux.NecroValleyFilter(f1),tp,LOCATION_GRAVE,0,1,1,nil,e,tp)
	if #g>0 then Duel.SpecialSummon(g,0,tp,tp,false,false,POS_FACEUP) end
end
function s.con2(e,tp) return Duel.IsPlayerCanDraw(tp,2) end
function s.op2(e,tp)
	if Duel.Draw(tp,2,REASON_EFFECT)==2 then
		Duel.ShuffleHand(tp)
		Duel.BreakEffect()
		Duel.DiscardHand(tp,nil,1,1,REASON_EFFECT+REASON_DISCARD)
	end
end
function s.initial_effect(c)
	--expert bot: the skill works by itself at the start of the owner's Main Phase 1
	local e1=Effect.CreateEffect(c)
	e1:SetProperty(EFFECT_FLAG_UNCOPYABLE+EFFECT_FLAG_CANNOT_DISABLE)
	e1:SetType(EFFECT_TYPE_FIELD+EFFECT_TYPE_CONTINUOUS)
	e1:SetCode(EVENT_STARTUP)
	e1:SetRange(0x5f)
	e1:SetOperation(aux.SetSkillOp(1,s.autocon,s.autoop,nil,EVENT_PHASE_START+PHASE_MAIN1))
	c:RegisterEffect(e1)
	aux.AddDrawless(c,false)
end
function s.autocon(e,tp) return Duel.GetTurnPlayer()==tp and Duel.GetCurrentChain()==0 and s.flipcon(e,tp) end
function s.autoop(e,tp)
	flip(tp)
	if not used(tp,1) and s.con1(e,tp) then use(tp,1) s.op1(e,tp)
	elseif not used(tp,2) and s.con2(e,tp) then use(tp,2) s.op2(e,tp) end
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
