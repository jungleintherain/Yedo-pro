--Ryzeal Overdrive (YEdo special skill - "Ryzeal" Structure Deck)
local s,id=GetID()
local function used(tp,n) return Duel.GetFlagEffect(tp,id+n*1000000)>0 end
local function use(tp,n) Duel.RegisterFlagEffect(tp,id+n*1000000,0,0,0) end
local function flip(tp) Duel.Hint(HINT_SKILL_FLIP,tp,id|(1<<32)) Duel.Hint(HINT_CARD,tp,id) end
local function mz(tp) return Duel.GetLocationCount(tp,LOCATION_MZONE)>0 end
local function tohand(g,tp) if #g>0 then Duel.SendtoHand(g,nil,REASON_EFFECT) Duel.ConfirmCards(1-tp,g) end end
local function f1(c,e,tp) return c:IsSetCard(0x1b6) and c:IsMonster() and c:IsCanBeSpecialSummoned(e,0,tp,false,false) end
function s.con1(e,tp) return mz(tp) and Duel.IsExistingMatchingCard(f1,tp,LOCATION_DECK,0,1,nil,e,tp) end
function s.op1(e,tp)
	Duel.Hint(HINT_SELECTMSG,tp,HINTMSG_SPSUMMON)
	local tc=Duel.SelectMatchingCard(tp,f1,tp,LOCATION_DECK,0,1,1,nil,e,tp):GetFirst()
	if tc and Duel.SpecialSummon(tc,0,tp,tp,false,false,POS_FACEUP)>0 and tc:HasLevel() then
		local e1=Effect.CreateEffect(e:GetHandler())
		e1:SetType(EFFECT_TYPE_SINGLE)
		e1:SetCode(EFFECT_CHANGE_LEVEL)
		e1:SetValue(4)
		e1:SetReset(RESET_EVENT|RESETS_STANDARD|RESET_PHASE|PHASE_END)
		tc:RegisterEffect(e1)
	end
end
local function mat(c) return c:IsSetCard(0x1b6) end
local function xyzf(c) return c:IsFaceup() and c:IsType(TYPE_XYZ) end
function s.con2(e,tp) return Duel.IsExistingMatchingCard(xyzf,tp,LOCATION_MZONE,0,1,nil) and Duel.IsExistingMatchingCard(mat,tp,LOCATION_GRAVE,0,1,nil) end
function s.op2(e,tp)
	Duel.Hint(HINT_SELECTMSG,tp,HINTMSG_FACEUP)
	local xc=Duel.SelectMatchingCard(tp,xyzf,tp,LOCATION_MZONE,0,1,1,nil):GetFirst()
	if not xc then return end
	Duel.Hint(HINT_SELECTMSG,tp,HINTMSG_XMATERIAL)
	local g=Duel.SelectMatchingCard(tp,mat,tp,LOCATION_GRAVE,0,1,2,nil)
	if #g>0 then Duel.Overlay(xc,g) end
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
