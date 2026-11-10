--Blaze of the Sun God (YEdo god skill)
local s,id=GetID()
local function used(tp,n) return Duel.GetFlagEffect(tp,id+n*1000000)>0 end
local function use(tp,n) Duel.RegisterFlagEffect(tp,id+n*1000000,0,0,0) end
local function flip(tp) Duel.Hint(HINT_SKILL_FLIP,tp,id|(1<<32)) Duel.Hint(HINT_CARD,tp,id) end
local function free(tp,n) return Duel.GetLocationCount(tp,LOCATION_MZONE)>=(n or 1) end
--Special Summon a card from outside of the Duel
local function outside(tp,code,pos)
	if not free(tp) then return nil end
	local tc=Duel.CreateToken(tp,code)
	if Duel.SpecialSummon(tc,0,tp,tp,true,false,pos or POS_FACEUP_ATTACK)>0 then return tc end
	return nil
end
--put cards from outside of the Duel in the Extra Deck
local function addextra(tp,codes)
	local g=Group.CreateGroup()
	for _,code in ipairs(codes) do g:AddCard(Duel.CreateToken(tp,code)) end
	Duel.SendtoDeck(g,tp,SEQ_DECKTOP,REASON_RULE)
end
local function exfilter(tp,codes)
	return function(c) return c:IsCode(table.unpack(codes)) and Duel.GetLocationCountFromEx(tp,tp,nil,c)>0 end
end
local function hasextra(tp,codes) return Duel.IsExistingMatchingCard(exfilter(tp,codes),tp,LOCATION_EXTRA,0,1,nil) end
--Special Summon from the Extra Deck ignoring its Summoning conditions (treated as a proper Summon)
local function fromextra(tp,codes,sumtype)
	Duel.Hint(HINT_SELECTMSG,tp,HINTMSG_SPSUMMON)
	local tc=Duel.SelectMatchingCard(tp,exfilter(tp,codes),tp,LOCATION_EXTRA,0,1,1,nil):GetFirst()
	if tc and Duel.SpecialSummon(tc,sumtype,tp,tp,true,false,POS_FACEUP_ATTACK)>0 then
		tc:CompleteProcedure()
		return tc
	end
	return nil
end
local function startup(c,op)
	local e1=Effect.CreateEffect(c)
	e1:SetProperty(EFFECT_FLAG_UNCOPYABLE+EFFECT_FLAG_CANNOT_DISABLE)
	e1:SetType(EFFECT_TYPE_FIELD+EFFECT_TYPE_CONTINUOUS)
	e1:SetCode(EVENT_STARTUP)
	e1:SetCountLimit(1)
	e1:SetRange(0x5f)
	e1:SetOperation(op)
	c:RegisterEffect(e1)
end
local function oppmon(tp) return Duel.GetFieldGroupCount(tp,0,LOCATION_MZONE)>0 end

local function godfilter(code) return function(c) return c:IsFaceup() and c:IsCode(code) end end
--the god stays on the field after the End Phase
local function godstay(c,tc)
	--its End Phase "send to the GY" trigger never activates (no prompt), it can still be destroyed normally
	local e1=Effect.CreateEffect(c)
	e1:SetType(EFFECT_TYPE_SINGLE)
	e1:SetCode(EFFECT_CANNOT_TRIGGER)
	e1:SetProperty(EFFECT_FLAG_SINGLE_RANGE+EFFECT_FLAG_CANNOT_DISABLE)
	e1:SetRange(LOCATION_MZONE)
	e1:SetCondition(function() return Duel.IsPhase(PHASE_END) end)
	e1:SetReset(RESET_EVENT|RESETS_STANDARD)
	tc:RegisterEffect(e1)
end
--Special Summon the god from the Deck (ignoring its summoning conditions)
local function deckgod(tp,code) return function(c) return c:IsCode(code) end end
local function hasgod(tp,code) return free(tp) and Duel.IsExistingMatchingCard(deckgod(tp,code),tp,LOCATION_DECK,0,1,nil) end
local function fromdeck(tp,code)
	if not free(tp) then return nil end
	Duel.Hint(HINT_SELECTMSG,tp,HINTMSG_SPSUMMON)
	local tc=Duel.SelectMatchingCard(tp,deckgod(tp,code),tp,LOCATION_DECK,0,1,1,nil):GetFirst()
	if tc and Duel.SpecialSummon(tc,0,tp,tp,true,false,POS_FACEUP_ATTACK)>0 then return tc end
	return nil
end
--this turn: cannot be targeted / unaffected by the opponent's card effects
local function godshield(c,tc)
	local e1=Effect.CreateEffect(c)
	e1:SetType(EFFECT_TYPE_SINGLE)
	e1:SetCode(EFFECT_CANNOT_BE_EFFECT_TARGET)
	e1:SetProperty(EFFECT_FLAG_SINGLE_RANGE+EFFECT_FLAG_CANNOT_DISABLE)
	e1:SetRange(LOCATION_MZONE)
	e1:SetValue(aux.tgoval)
	e1:SetReset(RESET_EVENT|RESETS_STANDARD|RESET_PHASE|PHASE_END)
	tc:RegisterEffect(e1)
	local e2=e1:Clone()
	e2:SetCode(EFFECT_IMMUNE_EFFECT)
	e2:SetValue(function(e,te) return te:GetOwnerPlayer()~=e:GetHandlerPlayer() end)
	tc:RegisterEffect(e2)
end

local GOD=10000010
function s.initial_effect(c)
	aux.AddSkillProcedure(c,1,false,s.flipcon,s.flipop)
	--Phoenix: once per Duel, when Ra is destroyed, it comes back
	startup(c,function(e,tp)
		local e1=Effect.CreateEffect(e:GetHandler())
		e1:SetType(EFFECT_TYPE_FIELD+EFFECT_TYPE_CONTINUOUS)
		e1:SetCode(EVENT_TO_GRAVE)
		e1:SetCondition(s.phcon)
		e1:SetOperation(s.phop)
		Duel.RegisterEffect(e1,tp)
	end)
end
local function rafilter(c,tp) return c:IsCode(GOD) and c:IsPreviousControler(tp) and c:IsPreviousLocation(LOCATION_MZONE) and c:IsReason(REASON_DESTROY) end
function s.phcon(e,tp,eg) return not used(tp,3) and eg:IsExists(rafilter,1,nil,tp) and free(tp) end
function s.phop(e,tp,eg)
	use(tp,3)
	flip(tp)
	local tc=eg:Filter(rafilter,nil,tp):GetFirst()
	if tc and Duel.SpecialSummon(tc,0,tp,tp,true,false,POS_FACEUP_ATTACK)>0 then
		local e1=Effect.CreateEffect(e:GetHandler())
		e1:SetType(EFFECT_TYPE_SINGLE)
		e1:SetCode(EFFECT_SET_ATTACK)
		e1:SetValue(4000)
		e1:SetReset(RESET_EVENT|RESETS_STANDARD)
		tc:RegisterEffect(e1)
		local e2=e1:Clone()
		e2:SetCode(EFFECT_SET_DEFENSE)
		tc:RegisterEffect(e2)
		godshield(e:GetHandler(),tc)
	end
end
local function b1(tp) return not used(tp,1) and hasgod(tp,GOD) and Duel.GetLP(tp)>100 end
local function b2(tp) return not used(tp,2) and oppmon(tp) and Duel.IsExistingMatchingCard(godfilter(GOD),tp,LOCATION_MZONE,0,1,nil) end
function s.flipcon(e,tp) return aux.CanActivateSkill(tp) and (b1(tp) or b2(tp)) end
function s.flipop(e,tp)
	flip(tp)
	local c=e:GetHandler()
	local op=Duel.SelectEffect(tp,{b1(tp),aux.Stringid(id,0)},{b2(tp),aux.Stringid(id,1)})
	if op==1 then
		use(tp,1)
		local tc=fromdeck(tp,GOD)
		if not tc then return end
		--pay LP until 100: Ra's ATK/DEF become the amount paid
		local paid=Duel.GetLP(tp)-100
		Duel.SetLP(tp,100)
		local e1=Effect.CreateEffect(c)
		e1:SetType(EFFECT_TYPE_SINGLE)
		e1:SetCode(EFFECT_SET_ATTACK)
		e1:SetValue(paid)
		e1:SetReset(RESET_EVENT|RESETS_STANDARD)
		tc:RegisterEffect(e1)
		local e2=e1:Clone()
		e2:SetCode(EFFECT_SET_DEFENSE)
		tc:RegisterEffect(e2)
		godshield(c,tc)
	elseif op==2 then
		use(tp,2)
		--Sun God's Wrath: attack every monster your opponent controls once this turn, and get LP back
		local g=Duel.GetMatchingGroup(godfilter(GOD),tp,LOCATION_MZONE,0,nil)
		for tc in g:Iter() do
			local e1=Effect.CreateEffect(c)
			e1:SetType(EFFECT_TYPE_SINGLE)
			e1:SetCode(EFFECT_ATTACK_ALL)
			e1:SetValue(1)
			e1:SetReset(RESET_EVENT|RESETS_STANDARD|RESET_PHASE|PHASE_END)
			tc:RegisterEffect(e1)
			godshield(c,tc)
			Duel.Recover(tp,math.max(0,tc:GetAttack()//2),REASON_EFFECT)
		end
	end
end
