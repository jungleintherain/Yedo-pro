import os
helpers=open('_helpers.lua').read()
skills={}
skills[300990010]=("Bonds of the Converging Stars",r'''
local SIGNERS={44508094,70902743,9012916,73580471,25862681}
function s.initial_effect(c)
	aux.AddSkillProcedure(c,1,false,s.flipcon,s.flipop)
	startup(c,function(e,tp) addextra(tp,SIGNERS) end)
end
local function b1(tp) return not used(tp,1) and free(tp,2) and Duel.IsExistingMatchingCard(Card.IsAbleToDeck,tp,LOCATION_HAND,0,1,nil) end
local function b2(tp) return not used(tp,2) and oppmon(tp) and hasextra(tp,SIGNERS) end
function s.flipcon(e,tp) return aux.CanActivateSkill(tp) and (b1(tp) or b2(tp)) end
function s.flipop(e,tp)
	flip(tp)
	local op=Duel.SelectEffect(tp,{b1(tp),aux.Stringid(id,0)},{b2(tp),aux.Stringid(id,1)})
	if op==1 then
		use(tp,1)
		local g=Duel.SelectMatchingCard(tp,Card.IsAbleToDeck,tp,LOCATION_HAND,0,1,1,nil)
		if #g==0 or Duel.SendtoDeck(g,nil,SEQ_DECKSHUFFLE,REASON_EFFECT)==0 then return end
		outside(tp,63977008) --Junk Synchron
		local tc=outside(tp,9365703) --Speed Warrior, treated as Level 5
		if tc then
			local e1=Effect.CreateEffect(e:GetHandler())
			e1:SetType(EFFECT_TYPE_SINGLE)
			e1:SetCode(EFFECT_CHANGE_LEVEL)
			e1:SetValue(5)
			e1:SetReset(RESET_EVENT|RESETS_STANDARD)
			tc:RegisterEffect(e1)
		end
	elseif op==2 then
		use(tp,2)
		fromextra(tp,SIGNERS,SUMMON_TYPE_SYNCHRO)
	end
end
''',["ส่งการ์ดในมือ 1 ใบกลับเด็ค: เรียก Junk Synchron + Speed Warrior (เลเวล 5)","เรียกมังกร Signer 1 ใบจาก Extra Deck"])

skills[300990011]=("Pharaoh's Destiny",r'''
function s.initial_effect(c)
	aux.AddSkillProcedure(c,1,false,s.flipcon,s.flipop)
end
local function b1(tp) return not used(tp,1) and Duel.IsExistingMatchingCard(Card.IsAbleToHand,tp,LOCATION_DECK,0,1,nil) end
local function b2(tp) return not used(tp,2) and free(tp) and Duel.GetFieldGroupCount(tp,LOCATION_MZONE,0)==0 end
function s.flipcon(e,tp) return aux.CanActivateSkill(tp) and (b1(tp) or b2(tp)) end
function s.flipop(e,tp)
	flip(tp)
	local op=Duel.SelectEffect(tp,{b1(tp),aux.Stringid(id,0)},{b2(tp),aux.Stringid(id,1)})
	if op==1 then
		use(tp,1)
		Duel.Hint(HINT_SELECTMSG,tp,HINTMSG_ATOHAND)
		local g=Duel.SelectMatchingCard(tp,Card.IsAbleToHand,tp,LOCATION_DECK,0,1,1,nil)
		if #g>0 then
			Duel.SendtoHand(g,nil,REASON_EFFECT)
			Duel.ConfirmCards(1-tp,g)
		end
	elseif op==2 then
		use(tp,2)
		outside(tp,46986414) --Dark Magician
		if free(tp) then outside(tp,38033120) end --Dark Magician Girl
	end
end
''',["เลือกการ์ดใดก็ได้จากเด็คขึ้นมือ 1 ใบ","ถ้าไม่มีมอนสเตอร์: เรียก Dark Magician + Dark Magician Girl"])

skills[300990012]=("Blue-Eyes Supremacy",r'''
function s.initial_effect(c)
	aux.AddSkillProcedure(c,1,false,s.flipcon,s.flipop)
	startup(c,function(e,tp) addextra(tp,{23995346}) end)
end
local function b1(tp) return not used(tp,1) and free(tp) end
local function b2(tp) return not used(tp,2) and oppmon(tp) and Duel.IsExistingMatchingCard(aux.FaceupFilter(Card.IsCode,89631139),tp,LOCATION_MZONE,0,1,nil) end
function s.flipcon(e,tp) return aux.CanActivateSkill(tp) and (b1(tp) or b2(tp)) end
function s.flipop(e,tp)
	flip(tp)
	local op=Duel.SelectEffect(tp,{b1(tp),aux.Stringid(id,0)},{b2(tp),aux.Stringid(id,1)})
	if op==1 then
		use(tp,1)
		outside(tp,89631139)
	elseif op==2 then
		use(tp,2)
		Duel.Destroy(Duel.GetFieldGroup(tp,0,LOCATION_MZONE),REASON_EFFECT)
		--only "Blue-Eyes White Dragon" can attack this turn
		local e1=Effect.CreateEffect(e:GetHandler())
		e1:SetType(EFFECT_TYPE_FIELD)
		e1:SetCode(EFFECT_CANNOT_ATTACK)
		e1:SetTargetRange(LOCATION_MZONE,0)
		e1:SetTarget(function(_,c) return not c:IsCode(89631139) end)
		e1:SetReset(RESET_PHASE|PHASE_END)
		Duel.RegisterEffect(e1,tp)
	end
end
''',["เรียก Blue-Eyes White Dragon จากนอกเกม","Burst Stream: ทำลายมอนสเตอร์คู่ต่อสู้ทั้งหมด (มี Blue-Eyes)"])

skills[300990013]=("Hero's Bond",r'''
local FUS={35809262,25366484}
function s.initial_effect(c)
	aux.AddSkillProcedure(c,1,false,s.flipcon,s.flipop)
	startup(c,function(e,tp) addextra(tp,FUS) end)
end
local function herofilter(c) return c:IsSetCard(0x3008) and c:IsMonster() and c:IsAbleToHand() end
local function b1(tp) return not used(tp,1) and oppmon(tp) and hasextra(tp,{35809262}) end
local function b2(tp) return not used(tp,2) end
function s.flipcon(e,tp) return aux.CanActivateSkill(tp) and (b1(tp) or b2(tp)) end
function s.flipop(e,tp)
	flip(tp)
	local op=Duel.SelectEffect(tp,{b1(tp),aux.Stringid(id,0)},{b2(tp),aux.Stringid(id,1)})
	if op==1 then
		use(tp,1)
		fromextra(tp,{35809262},SUMMON_TYPE_FUSION)
	elseif op==2 then
		use(tp,2)
		local poly=Duel.CreateToken(tp,24094653)
		local g=Group.FromCards(poly)
		local hg=Duel.GetMatchingGroup(herofilter,tp,LOCATION_DECK,0,nil)
		if #hg>0 then
			Duel.Hint(HINT_SELECTMSG,tp,HINTMSG_ATOHAND)
			g:Merge(hg:Select(tp,1,1,nil))
		end
		Duel.SendtoHand(g,nil,REASON_EFFECT)
		Duel.ConfirmCards(1-tp,g)
	end
end
''',["เรียก Elemental HERO Flame Wingman จาก Extra Deck","ได้ Polymerization + Elemental HERO 1 ใบจากเด็คขึ้นมือ"])

skills[300990014]=("Burning Soul of the King",r'''
function s.initial_effect(c)
	aux.AddSkillProcedure(c,1,false,s.flipcon,s.flipop)
	startup(c,function(e,tp) addextra(tp,{70902743}) end)
end
local function b1(tp) return not used(tp,1) and hasextra(tp,{70902743}) end
local function b2(tp) return not used(tp,2) and Duel.IsExistingMatchingCard(aux.FaceupFilter(Card.IsCode,70902743),tp,LOCATION_MZONE,0,1,nil)
	and Duel.IsExistingMatchingCard(Card.IsDefensePos,tp,0,LOCATION_MZONE,1,nil) end
function s.flipcon(e,tp) return aux.CanActivateSkill(tp) and (b1(tp) or b2(tp)) end
function s.flipop(e,tp)
	flip(tp)
	local op=Duel.SelectEffect(tp,{b1(tp),aux.Stringid(id,0)},{b2(tp),aux.Stringid(id,1)})
	if op==1 then
		use(tp,1)
		local tc=fromextra(tp,{70902743},SUMMON_TYPE_SYNCHRO)
		if tc then
			local e1=Effect.CreateEffect(e:GetHandler())
			e1:SetType(EFFECT_TYPE_SINGLE)
			e1:SetCode(EFFECT_UPDATE_ATTACK)
			e1:SetValue(1000)
			e1:SetReset(RESET_EVENT|RESETS_STANDARD)
			tc:RegisterEffect(e1)
		end
	elseif op==2 then
		use(tp,2)
		local n=Duel.Destroy(Duel.GetMatchingGroup(Card.IsDefensePos,tp,0,LOCATION_MZONE,nil),REASON_EFFECT)
		if n>0 then Duel.Damage(1-tp,n*500,REASON_EFFECT) end
	end
end
''',["เรียก Red Dragon Archfiend จาก Extra Deck (ATK +1000)","Absolute Power Force: ทำลายมอนสเตอร์ท่าป้องกันฝ่ายตรงข้าม + 500 ดาเมจ/ใบ"])

skills[300990015]=("Overflowing Hope",r'''
function s.initial_effect(c)
	aux.AddSkillProcedure(c,1,-2,nil,nil)
	startup(c,function(e,tp)
		flip(tp)
		Duel.SetLP(tp,Duel.GetLP(tp)+2000)
	end)
end
''',[])

skills[300990016]=("Heavenly Reversal",r'''
function s.initial_effect(c)
	aux.AddSkillProcedure(c,1,false,s.flipcon,s.flipop)
end
function s.flipcon(e,tp)
	return aux.CanActivateSkill(tp) and not used(tp,1) and Duel.IsPlayerCanDraw(tp,3)
		and (Duel.GetLP(tp)*2<=Duel.GetLP(1-tp) or Duel.GetLP(tp)<=2000)
end
function s.flipop(e,tp)
	flip(tp)
	use(tp,1)
	Duel.Draw(tp,3,REASON_EFFECT)
	Duel.Recover(tp,1000,REASON_EFFECT)
end
''',[])

skills[300990017]=("Soul Revival",r'''
function s.initial_effect(c)
	aux.AddSkillProcedure(c,1,false,s.flipcon,s.flipop)
end
local function spfilter(c,e,tp) return c:IsMonster() and c:IsCanBeSpecialSummoned(e,0,tp,false,false) end
function s.flipcon(e,tp)
	return aux.CanActivateSkill(tp) and not used(tp,1) and free(tp)
		and Duel.IsExistingMatchingCard(spfilter,tp,LOCATION_GRAVE,LOCATION_GRAVE,1,nil,e,tp)
end
function s.flipop(e,tp)
	flip(tp)
	use(tp,1)
	Duel.Hint(HINT_SELECTMSG,tp,HINTMSG_SPSUMMON)
	local g=Duel.SelectMatchingCard(tp,spfilter,tp,LOCATION_GRAVE,LOCATION_GRAVE,1,1,nil,e,tp)
	if #g>0 then Duel.SpecialSummon(g,0,tp,tp,false,false,POS_FACEUP_ATTACK) end
end
''',[])

skills[300990018]=("Judgment of Thunder",r'''
function s.initial_effect(c)
	aux.AddSkillProcedure(c,1,false,s.flipcon,s.flipop)
end
function s.flipcon(e,tp)
	return aux.CanActivateSkill(tp) and not used(tp,1)
		and Duel.GetFieldGroupCount(tp,0,LOCATION_MZONE)>=Duel.GetFieldGroupCount(tp,LOCATION_MZONE,0)+2
end
function s.flipop(e,tp)
	flip(tp)
	use(tp,1)
	Duel.Destroy(Duel.GetFieldGroup(tp,0,LOCATION_MZONE),REASON_EFFECT)
end
''',[])

skills[300990019]=("Rank-Up Hope",r'''
function s.initial_effect(c)
	aux.AddSkillProcedure(c,1,false,s.flipcon,s.flipop)
	startup(c,function(e,tp) addextra(tp,{84013237}) end)
end
function s.flipcon(e,tp) return aux.CanActivateSkill(tp) and not used(tp,1) and hasextra(tp,{84013237}) end
function s.flipop(e,tp)
	flip(tp)
	use(tp,1)
	local tc=fromextra(tp,{84013237},SUMMON_TYPE_XYZ)
	if tc then
		local g=Duel.GetDecktopGroup(tp,2)
		if #g>0 then
			Duel.DisableShuffleCheck()
			Duel.Overlay(tc,g)
		end
	end
end
''',[])

skills[300990020]=("Pendulum Dimension",r'''
function s.initial_effect(c)
	aux.AddSkillProcedure(c,1,false,s.flipcon,s.flipop)
end
local function pfilter(c) return c:IsType(TYPE_PENDULUM) and c:IsAbleToHand() end
function s.flipcon(e,tp) return aux.CanActivateSkill(tp) and not used(tp,1) and free(tp) end
function s.flipop(e,tp)
	flip(tp)
	use(tp,1)
	outside(tp,16178681) --Odd-Eyes Pendulum Dragon
	local g=Duel.GetMatchingGroup(pfilter,tp,LOCATION_DECK,0,nil)
	if #g>0 then
		Duel.Hint(HINT_SELECTMSG,tp,HINTMSG_ATOHAND)
		local sg=g:Select(tp,1,1,nil)
		Duel.SendtoHand(sg,nil,REASON_EFFECT)
		Duel.ConfirmCards(1-tp,sg)
	end
end
''',[])

skills[300990021]=("Storm Access",r'''
function s.initial_effect(c)
	aux.AddSkillProcedure(c,1,false,s.flipcon,s.flipop)
	startup(c,function(e,tp) addextra(tp,{1861629,5043010}) end)
end
local function b1(tp) return not used(tp,1) and oppmon(tp) and hasextra(tp,{1861629}) end
local function b2(tp) return not used(tp,2) and Duel.GetLP(tp)<Duel.GetLP(1-tp) and hasextra(tp,{5043010}) end
function s.flipcon(e,tp) return aux.CanActivateSkill(tp) and (b1(tp) or b2(tp)) end
function s.flipop(e,tp)
	flip(tp)
	local op=Duel.SelectEffect(tp,{b1(tp),aux.Stringid(id,0)},{b2(tp),aux.Stringid(id,1)})
	if op==1 then
		use(tp,1)
		fromextra(tp,{1861629},SUMMON_TYPE_LINK)
	elseif op==2 then
		use(tp,2)
		fromextra(tp,{5043010},SUMMON_TYPE_LINK)
	end
end
''',["เรียก Decode Talker จาก Extra Deck","LP น้อยกว่าคู่ต่อสู้: เรียก Firewall Dragon จาก Extra Deck"])

import json
os.makedirs('script',exist_ok=True)
for code,(name,body,strs) in skills.items():
    src=f"--{name} (story mod skill)\nlocal s,id=GetID()\n"+helpers+body
    open(f'script/c{code}.lua','w').write(src)
json.dump({str(k):[v[0],v[2]] for k,v in skills.items()},open('meta.json','w'),ensure_ascii=False)
print(len(skills))
