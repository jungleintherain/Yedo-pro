--Vision of the Future (YEdo special skill - "Vision HERO" Structure Deck)
local s,id=GetID()
local function used(tp,n) return Duel.GetFlagEffect(tp,id+n*1000000)>0 end
local function use(tp,n) Duel.RegisterFlagEffect(tp,id+n*1000000,0,0,0) end
local function flip(tp) Duel.Hint(HINT_SKILL_FLIP,tp,id|(1<<32)) Duel.Hint(HINT_CARD,tp,id) end
local function mz(tp) return Duel.GetLocationCount(tp,LOCATION_MZONE)>0 end
local function tohand(g,tp) if #g>0 then Duel.SendtoHand(g,nil,REASON_EFFECT) Duel.ConfirmCards(1-tp,g) end end
local function f1(c) return c:IsSetCard(0x5008) and c:IsMonster() and not c:IsForbidden() end
function s.con1(e,tp) return Duel.GetLocationCount(tp,LOCATION_SZONE)>0 and Duel.IsExistingMatchingCard(f1,tp,LOCATION_DECK,0,1,nil) end
function s.op1(e,tp)
	Duel.Hint(HINT_SELECTMSG,tp,HINTMSG_TOFIELD)
	local tc=Duel.SelectMatchingCard(tp,f1,tp,LOCATION_DECK,0,1,1,nil):GetFirst()
	if tc and Duel.MoveToField(tc,tp,tp,LOCATION_SZONE,POS_FACEUP,true) then
		local e1=Effect.CreateEffect(e:GetHandler())
		e1:SetType(EFFECT_TYPE_SINGLE)
		e1:SetCode(EFFECT_CHANGE_TYPE)
		e1:SetProperty(EFFECT_FLAG_CANNOT_DISABLE)
		e1:SetValue(TYPE_TRAP+TYPE_CONTINUOUS)
		e1:SetReset(RESET_EVENT|(RESETS_STANDARD&~RESET_TURN_SET))
		tc:RegisterEffect(e1)
	end
end
local function f2(c,e,tp) return c:IsSetCard(0x8) and c:GetOriginalType()&TYPE_MONSTER~=0 and (c:IsLocation(LOCATION_GRAVE) or c:IsFaceup())
	and c:IsCanBeSpecialSummoned(e,0,tp,false,false) end
function s.con2(e,tp) return mz(tp) and Duel.IsExistingMatchingCard(f2,tp,LOCATION_SZONE+LOCATION_GRAVE,0,1,nil,e,tp) end
function s.op2(e,tp)
	Duel.Hint(HINT_SELECTMSG,tp,HINTMSG_SPSUMMON)
	local g=Duel.SelectMatchingCard(tp,aux.NecroValleyFilter(f2),tp,LOCATION_SZONE+LOCATION_GRAVE,0,1,1,nil,e,tp)
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
