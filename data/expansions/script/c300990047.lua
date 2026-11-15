--Sky Striker Sortie (YEdo special skill - "Sky Striker" Structure Deck)
local s,id=GetID()
local function used(tp,n) return Duel.GetFlagEffect(tp,id+n*1000000)>0 end
local function use(tp,n) Duel.RegisterFlagEffect(tp,id+n*1000000,0,0,0) end
local function flip(tp) Duel.Hint(HINT_SKILL_FLIP,tp,id|(1<<32)) Duel.Hint(HINT_CARD,tp,id) end
local function mz(tp) return Duel.GetLocationCount(tp,LOCATION_MZONE)>0 end
local function tohand(g,tp) if #g>0 then Duel.SendtoHand(g,nil,REASON_EFFECT) Duel.ConfirmCards(1-tp,g) end end
local function nomain(tp) return not Duel.IsExistingMatchingCard(function(c) return c:GetSequence()<5 end,tp,LOCATION_MZONE,0,1,nil) end
local function f1(c) return c:IsSetCard(0x115) and c:IsSpell() and c:IsAbleToHand() end
function s.con1(e,tp) return nomain(tp) and Duel.IsExistingMatchingCard(f1,tp,LOCATION_DECK,0,1,nil) end
function s.op1(e,tp)
	Duel.Hint(HINT_SELECTMSG,tp,HINTMSG_ATOHAND)
	tohand(Duel.SelectMatchingCard(tp,f1,tp,LOCATION_DECK,0,1,1,nil),tp)
end
local function f2(c,e,tp) return c:IsCode(26077387) and c:IsCanBeSpecialSummoned(e,0,tp,false,false) end
function s.con2(e,tp) return mz(tp) and Duel.IsExistingMatchingCard(f2,tp,LOCATION_HAND+LOCATION_DECK+LOCATION_GRAVE,0,1,nil,e,tp) end
function s.op2(e,tp)
	Duel.Hint(HINT_SELECTMSG,tp,HINTMSG_SPSUMMON)
	local g=Duel.SelectMatchingCard(tp,aux.NecroValleyFilter(f2),tp,LOCATION_HAND+LOCATION_DECK+LOCATION_GRAVE,0,1,1,nil,e,tp)
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
