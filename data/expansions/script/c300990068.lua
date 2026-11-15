--Rival's Resolve (YEdo HARD bot skill, automatic)
local s,id=GetID()
local function used(tp,n) return Duel.GetFlagEffect(tp,id+n*1000000)>0 end
local function use(tp,n) Duel.RegisterFlagEffect(tp,id+n*1000000,0,0,0) end
local function flip(tp) Duel.Hint(HINT_SKILL_FLIP,tp,id|(1<<32)) Duel.Hint(HINT_CARD,tp,id) end
function s.initial_effect(c)
	local e1=Effect.CreateEffect(c)
	e1:SetProperty(EFFECT_FLAG_UNCOPYABLE+EFFECT_FLAG_CANNOT_DISABLE)
	e1:SetType(EFFECT_TYPE_FIELD+EFFECT_TYPE_CONTINUOUS)
	e1:SetCode(EVENT_STARTUP)
	e1:SetRange(0x5f)
	e1:SetOperation(aux.SetSkillOp(1,s.autocon,s.autoop,nil,EVENT_PHASE_START+PHASE_MAIN1))
	c:RegisterEffect(e1)
	aux.AddDrawless(c,false)
end
function s.autocon(e,tp) return Duel.GetTurnPlayer()==tp and Duel.GetCurrentChain()==0 and ((not used(tp,1) and s.con1(e,tp)) or (not used(tp,2) and s.con2(e,tp))) end
function s.autoop(e,tp)
	flip(tp)
	if not used(tp,1) and s.con1(e,tp) then use(tp,1) s.op1(e,tp)
	elseif not used(tp,2) and s.con2(e,tp) then use(tp,2) s.op2(e,tp) end
end
function s.con1(e,tp) return Duel.GetFieldGroupCount(tp,LOCATION_HAND,0)<=2 and Duel.IsPlayerCanDraw(tp,1) end
function s.op1(e,tp) Duel.Draw(tp,1,REASON_EFFECT) end
function s.con2(e,tp) return Duel.GetLP(tp)<=4000 end
function s.op2(e,tp) Duel.Recover(tp,2000,REASON_EFFECT) end
