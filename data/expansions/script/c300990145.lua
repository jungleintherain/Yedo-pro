--Tears of the Abyss (YEdo EXPERT bot skill, automatic - "Tearlaments" Structure Deck)
local s,id=GetID()
local function used(tp,n) return Duel.GetFlagEffect(tp,id+n*1000000)>0 end
local function use(tp,n) Duel.RegisterFlagEffect(tp,id+n*1000000,0,0,0) end
local function flip(tp) Duel.Hint(HINT_SKILL_FLIP,tp,id|(1<<32)) Duel.Hint(HINT_CARD,tp,id) end
local function mz(tp) return Duel.GetLocationCount(tp,LOCATION_MZONE)>0 end
local function tohand(g,tp) if #g>0 then Duel.SendtoHand(g,nil,REASON_EFFECT) Duel.ConfirmCards(1-tp,g) end end
function s.con1(e,tp) return Duel.IsPlayerCanDiscardDeck(tp,3) end
function s.op1(e,tp) Duel.DiscardDeck(tp,3,REASON_EFFECT) end
local function f2(c) return c:IsSetCard(0x182) and c:IsAbleToDeck() end
function s.con2(e,tp) return Duel.IsPlayerCanDraw(tp,1) and Duel.IsExistingMatchingCard(f2,tp,LOCATION_GRAVE,0,1,nil) end
function s.op2(e,tp)
	Duel.Hint(HINT_SELECTMSG,tp,HINTMSG_TODECK)
	local g=Duel.SelectMatchingCard(tp,aux.NecroValleyFilter(f2),tp,LOCATION_GRAVE,0,1,3,nil)
	if #g>0 and Duel.SendtoDeck(g,nil,SEQ_DECKSHUFFLE,REASON_EFFECT)>0 then
		Duel.ShuffleDeck(tp)
		Duel.BreakEffect()
		Duel.Draw(tp,1,REASON_EFFECT)
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
