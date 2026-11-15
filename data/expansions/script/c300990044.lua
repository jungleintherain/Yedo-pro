--Welcome to the Labyrinth (YEdo special skill - "Labrynth" Structure Deck)
local s,id=GetID()
local function used(tp,n) return Duel.GetFlagEffect(tp,id+n*1000000)>0 end
local function use(tp,n) Duel.RegisterFlagEffect(tp,id+n*1000000,0,0,0) end
local function flip(tp) Duel.Hint(HINT_SKILL_FLIP,tp,id|(1<<32)) Duel.Hint(HINT_CARD,tp,id) end
local function mz(tp) return Duel.GetLocationCount(tp,LOCATION_MZONE)>0 end
local function tohand(g,tp) if #g>0 then Duel.SendtoHand(g,nil,REASON_EFFECT) Duel.ConfirmCards(1-tp,g) end end
local function f1(c) return c:GetType()==TYPE_TRAP and c:IsSSetable() end
function s.con1(e,tp) return Duel.GetLocationCount(tp,LOCATION_SZONE)>0 and Duel.IsExistingMatchingCard(f1,tp,LOCATION_DECK,0,1,nil) end
function s.op1(e,tp)
	Duel.Hint(HINT_SELECTMSG,tp,HINTMSG_SET)
	local tc=Duel.SelectMatchingCard(tp,f1,tp,LOCATION_DECK,0,1,1,nil):GetFirst()
	if tc and Duel.SSet(tp,tc)>0 then
		local e1=Effect.CreateEffect(e:GetHandler())
		e1:SetType(EFFECT_TYPE_SINGLE)
		e1:SetCode(EFFECT_TRAP_ACT_IN_SET_TURN)
		e1:SetProperty(EFFECT_FLAG_SET_AVAILABLE)
		e1:SetReset(RESET_EVENT|RESETS_STANDARD|RESET_PHASE|PHASE_END)
		tc:RegisterEffect(e1)
	end
end
local function f2(c,e,tp) return c:IsSetCard(0x17f) and c:IsMonster() and c:IsCanBeSpecialSummoned(e,0,tp,false,false) end
function s.con2(e,tp) return mz(tp) and Duel.IsExistingMatchingCard(f2,tp,LOCATION_HAND+LOCATION_DECK,0,1,nil,e,tp) end
function s.op2(e,tp)
	Duel.Hint(HINT_SELECTMSG,tp,HINTMSG_SPSUMMON)
	local g=Duel.SelectMatchingCard(tp,f2,tp,LOCATION_HAND+LOCATION_DECK,0,1,1,nil,e,tp)
	if #g>0 then Duel.SpecialSummon(g,0,tp,tp,false,false,POS_FACEUP) end
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
