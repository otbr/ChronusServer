/**
 * The Forgotten Server - a free and open-source MMORPG server emulator
 * Copyright (C) 2014  Mark Samman <mark.samman@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "otpch.h"

#include "combat.h"
#include "configmanager.h"
#include "game.h"
#include "pugicast.h"
#include "tools.h"
#include "weapons.h"

extern Game g_game;
extern Vocations g_vocations;
extern ConfigManager g_config;
extern Weapons* g_weapons;

Weapons::Weapons():
	m_scriptInterface("Weapon Interface")
{
	m_scriptInterface.initState();
}

Weapons::~Weapons()
{
	clear();
}

const Weapon* Weapons::getWeapon(const Item* item) const
{
	if (!item) {
		return nullptr;
	}

	auto it = weapons.find(item->getID());
	if (it == weapons.end()) {
		return nullptr;
	}
	return it->second;
}

void Weapons::clear()
{
	for (const auto& it : weapons) {
		delete it.second;
	}
	weapons.clear();

	m_scriptInterface.reInitState();
}

LuaScriptInterface& Weapons::getScriptInterface()
{
	return m_scriptInterface;
}

std::string Weapons::getScriptBaseName() const
{
	return "weapons";
}

void Weapons::loadDefaults()
{
	for (size_t i = 100, size = Item::items.size(); i < size; ++i) {
		const ItemType& it = Item::items.getItemType(i);
		if (it.id == 0 || weapons.find(i) != weapons.end()) {
			continue;
		}

		if (it.weaponType != WEAPON_NONE) {
			switch (it.weaponType) {
				case WEAPON_AXE:
				case WEAPON_SWORD:
				case WEAPON_CLUB: {
					WeaponMelee* weapon = new WeaponMelee(&m_scriptInterface);
					weapon->configureWeapon(it);
					weapons[i] = weapon;
					break;
				}

				case WEAPON_AMMO:
				case WEAPON_DISTANCE: {
					if (it.weaponType == WEAPON_DISTANCE && it.ammoType != AMMO_NONE) {
						continue;
					}

					WeaponDistance* weapon = new WeaponDistance(&m_scriptInterface);
					weapon->configureWeapon(it);
					weapons[i] = weapon;
					break;
				}

				default:
					break;
			}
		}
	}
}

Event* Weapons::getEvent(const std::string& nodeName)
{
	std::string tmpNodeName = asLowerCaseString(nodeName);
	if (tmpNodeName == "melee") {
		return new WeaponMelee(&m_scriptInterface);
	} else if (tmpNodeName == "distance") {
		return new WeaponDistance(&m_scriptInterface);
	} else if (tmpNodeName == "wand" || tmpNodeName == "rod") {
		return new WeaponWand(&m_scriptInterface);
	}
	return nullptr;
}

bool Weapons::registerEvent(Event* event, const pugi::xml_node&)
{
	Weapon* weapon = reinterpret_cast<Weapon*>(event);
	if (weapons.find(weapon->getID()) != weapons.end()) {
		std::cout << "[Warning - Weapons::registerEvent] Duplicate registered item with id: " << weapon->getID() << std::endl;
		return false;
	}

	weapons[weapon->getID()] = weapon;
	return true;
}

//monsters
int32_t Weapons::getMaxMeleeDamage(int32_t attackSkill, int32_t attackValue)
{
	return static_cast<int32_t>(std::ceil((attackSkill * (attackValue * 0.05)) + (attackValue * 0.5)));
}

//players
int32_t Weapons::getMaxWeaponDamage(uint32_t level, int32_t attackSkill, int32_t attackValue, float attackFactor)
{
	return static_cast<int32_t>(std::ceil((2 * (attackValue * (attackSkill + 5.8) / 25 + (level - 1) / 10.)) / attackFactor));
}

Weapon::Weapon(LuaScriptInterface* _interface) :
	Event(_interface)
{
	m_scripted = false;
	id = 0;
	level = 0;
	magLevel = 0;
	mana = 0;
	manaPercent = 0;
	soul = 0;
	premium = false;
	enabled = true;
	wieldUnproperly = false;
	range = 1;
	ammoAction = AMMOACTION_NONE;
}

Weapon::~Weapon()
{
	//
}

bool Weapon::configureEvent(const pugi::xml_node& node)
{
	pugi::xml_attribute attr;
	if (!(attr = node.attribute("id"))) {
		std::cout << "[Error - Weapon::configureEvent] Weapon without id." << std::endl;
		return false;
	}
	id = pugi::cast<uint16_t>(attr.value());

	if ((attr = node.attribute("level"))) {
		level = pugi::cast<int32_t>(attr.value());
	}

	if ((attr = node.attribute("maglv")) || (attr = node.attribute("maglevel"))) {
		magLevel = pugi::cast<int32_t>(attr.value());
	}

	if ((attr = node.attribute("mana"))) {
		mana = pugi::cast<int32_t>(attr.value());
	}

	if ((attr = node.attribute("manapercent"))) {
		manaPercent = pugi::cast<int32_t>(attr.value());
	}

	if ((attr = node.attribute("soul"))) {
		soul = pugi::cast<int32_t>(attr.value());
	}

	if ((attr = node.attribute("prem"))) {
		premium = attr.as_bool();
	}

	if ((attr = node.attribute("enabled"))) {
		enabled = attr.as_bool();
	}

	if ((attr = node.attribute("unproperly"))) {
		wieldUnproperly = attr.as_bool();
	}

	std::list<std::string> vocStringList;
	for (pugi::xml_node vocationNode = node.first_child(); vocationNode; vocationNode = vocationNode.next_sibling()) {
		if (!(attr = vocationNode.attribute("name"))) {
			continue;
		}

		int32_t vocationId = g_vocations.getVocationId(attr.as_string());
		if (vocationId != -1) {
			vocWeaponMap[vocationId] = true;
			int32_t promotedVocation = g_vocations.getPromotedVocation(vocationId);
			if (promotedVocation != 0) {
				vocWeaponMap[promotedVocation] = true;
			}

			if (vocationNode.attribute("showInDescription").as_bool(true)) {
				vocStringList.push_back(asLowerCaseString(attr.as_string()));
			}
		}
	}
	range = Item::items[id].shootRange;

	std::string vocationString;
	for (const std::string& str : vocStringList) {
		if (!vocationString.empty()) {
			if (str != vocStringList.back()) {
				vocationString.push_back(',');
				vocationString.push_back(' ');
			} else {
				vocationString += " and ";
			}
		}

		vocationString += str;
		vocationString.push_back('s');
	}

	uint32_t wieldInfo = 0;
	if (getReqLevel() > 0) {
		wieldInfo |= WIELDINFO_LEVEL;
	}

	if (getReqMagLv() > 0) {
		wieldInfo |= WIELDINFO_MAGLV;
	}

	if (!vocationString.empty()) {
		wieldInfo |= WIELDINFO_VOCREQ;
	}

	if (isPremium()) {
		wieldInfo |= WIELDINFO_PREMIUM;
	}

	if (wieldInfo != 0) {
		ItemType& it = Item::items.getItemType(id);
		it.wieldInfo = wieldInfo;
		it.vocationString = vocationString;
		it.minReqLevel = getReqLevel();
		it.minReqMagicLevel = getReqMagLv();
	}
	return configureWeapon(Item::items[getID()]);
}

bool Weapon::loadFunction(const std::string& functionName)
{
	std::string tmpFunctionName = asLowerCaseString(functionName);
	if (tmpFunctionName == "internalloadweapon" || tmpFunctionName == "default") {
		if (configureWeapon(Item::items[getID()])) {
			return true;
		}
	} else if (tmpFunctionName == "script") {
		m_scripted = true;
	}
	return false;
}

bool Weapon::configureWeapon(const ItemType& it)
{
	id = it.id;
	return true;
}

std::string Weapon::getScriptEventName() const
{
	return "onUseWeapon";
}

int32_t Weapon::playerWeaponCheck(Player* player, Creature* target) const
{
	const Position& playerPos = player->getPosition();
	const Position& targetPos = target->getPosition();
	if (playerPos.z != targetPos.z) {
		return 0;
	}

	uint8_t trueRange;
	const ItemType& it = Item::items[getID()];
	if (it.weaponType == WEAPON_AMMO) {
		trueRange = player->getShootRange();
	} else {
		trueRange = range;
	}

	if (std::max<uint32_t>(Position::getDistanceX(playerPos, targetPos), Position::getDistanceY(playerPos, targetPos)) > trueRange) {
		return 0;
	}

	if (!player->hasFlag(PlayerFlag_IgnoreWeaponCheck)) {
		if (!enabled) {
			return 0;
		}

		if (player->getMana() < getManaCost(player)) {
			return 0;
		}

		if (player->getSoul() < soul) {
			return 0;
		}

		if (isPremium() && !player->isPremium()) {
			return 0;
		}

		if (!vocWeaponMap.empty()) {
			if (vocWeaponMap.find(player->getVocationId()) == vocWeaponMap.end()) {
				return 0;
			}
		}

		int32_t damageModifier = 100;
		if (player->getLevel() < getReqLevel()) {
			damageModifier = (isWieldedUnproperly() ? damageModifier / 2 : 0);
		}

		if (player->getMagicLevel() < getReqMagLv()) {
			damageModifier = (isWieldedUnproperly() ? damageModifier / 2 : 0);
		}
		return damageModifier;
	}

	return 100;
}

bool Weapon::useWeapon(Player* player, Item* item, Creature* target) const
{
	int32_t damageModifier = playerWeaponCheck(player, target);
	if (damageModifier == 0) {
		return false;
	}
	return internalUseWeapon(player, item, target, damageModifier);
}

bool Weapon::useFist(Player* player, Creature* target)
{
	if (!Position::areInRange<1, 1>(player->getPosition(), target->getPosition())) {
		return false;
	}

	float attackFactor = player->getAttackFactor();
	int32_t attackSkill = player->getSkill(SKILL_FIST, SKILLVALUE_LEVEL);
	int32_t attackValue = 7;

	int32_t maxDamage = Weapons::getMaxWeaponDamage(player->getLevel(), attackSkill, attackValue, attackFactor);

	CombatParams params;
	params.combatType = COMBAT_PHYSICALDAMAGE;
	params.blockedByArmor = true;
	params.blockedByShield = true;

	CombatDamage damage;
	damage.origin = ORIGIN_MELEE;
	damage.primary.type = params.combatType;
	damage.primary.value = -normal_random(0, maxDamage);

	Combat::doCombatHealth(player, target, damage, params);
	if (!player->hasFlag(PlayerFlag_NotGainSkill) && player->getAddAttackSkill()) {
		player->addSkillAdvance(SKILL_FIST, 1);
	}

	return true;
}

bool Weapon::internalUseWeapon(Player* player, Item* item, Creature* target, int32_t damageModifier) const
{
	if (m_scripted) {
		LuaVariant var;
		var.type = VARIANT_NUMBER;
		var.number = target->getID();
		executeUseWeapon(player, var);
	} else {
		CombatDamage damage;
		WeaponType_t weaponType = item->getWeaponType();
		if (weaponType == WEAPON_AMMO || weaponType == WEAPON_DISTANCE) {
			damage.origin = ORIGIN_RANGED;
		} else {
			damage.origin = ORIGIN_MELEE;
		}
		damage.primary.type = params.combatType;
		damage.primary.value = (getWeaponDamage(player, target, item) * damageModifier) / 100;
		damage.secondary.type = getElementType();
		damage.secondary.value = getElementDamage(player, target, item);
		Combat::doCombatHealth(player, target, damage, params);
	}

	onUsedAmmo(item, target->getTile());
	onUsedWeapon(player, item);
	return true;
}

bool Weapon::internalUseWeapon(Player* player, Item* item, Tile* tile) const
{
	if (m_scripted) {
		LuaVariant var;
		var.type = VARIANT_TARGETPOSITION;
		var.pos = tile->getPosition();
		executeUseWeapon(player, var);
	} else {
		Combat::postCombatEffects(player, tile->getPosition(), params);
		g_game.addMagicEffect(tile->getPosition(), CONST_ME_POFF);
	}

	onUsedAmmo(item, tile);
	onUsedWeapon(player, item);
	return true;
}

void Weapon::onUsedWeapon(Player* player, Item* item) const
{
	if (!player->hasFlag(PlayerFlag_NotGainSkill)) {
		skills_t skillType;
		uint32_t skillPoint;
		if (getSkillType(player, item, skillType, skillPoint)) {
			player->addSkillAdvance(skillType, skillPoint);
		}
	}

	uint32_t manaCost = getManaCost(player);
	if (manaCost != 0) {
		player->addManaSpent(manaCost);

		player->changeMana(-static_cast<int32_t>(manaCost));
	}

	if (!player->hasFlag(PlayerFlag_HasInfiniteSoul) && soul > 0) {
		player->changeSoul(-static_cast<int32_t>(soul));
	}
}

void Weapon::onUsedAmmo(Item* item, Tile* destTile) const
{
	if (!g_config.getBoolean(ConfigManager::REMOVE_AMMO)) {
		return;
	}

	if (ammoAction == AMMOACTION_REMOVECOUNT) {
		uint16_t newCount = item->getItemCount();
		if (newCount > 0) {
			newCount--;
		}

		g_game.transformItem(item, item->getID(), newCount);
	} else if (ammoAction == AMMOACTION_MOVE) {
		g_game.internalMoveItem(item->getParent(), destTile, INDEX_WHEREEVER, item, 1, nullptr, FLAG_NOLIMIT);
	} else if (ammoAction == AMMOACTION_MOVEBACK) {
		/* do nothing */
	} else {
		/* remove charges */
		uint16_t charges = item->getCharges();
		if (charges != 0) {
			g_game.transformItem(item, item->getID(), charges - 1);
		}
	}
}

uint32_t Weapon::getManaCost(const Player* player) const
{
	if (mana != 0) {
		return mana;
	}

	if (manaPercent == 0) {
		return 0;
	}

	return (player->getMaxMana() * manaPercent) / 100;
}

bool Weapon::executeUseWeapon(Player* player, const LuaVariant& var) const
{
	//onUseWeapon(player, var)
	if (!m_scriptInterface->reserveScriptEnv()) {
		std::cout << "[Error - Weapon::executeUseWeapon] Call stack overflow" << std::endl;
		return false;
	}

	ScriptEnvironment* env = m_scriptInterface->getScriptEnv();
	env->setScriptId(m_scriptId, m_scriptInterface);

	lua_State* L = m_scriptInterface->getLuaState();

	m_scriptInterface->pushFunction(m_scriptId);
	LuaScriptInterface::pushUserdata<Player>(L, player);
	LuaScriptInterface::setMetatable(L, -1, "Player");
	m_scriptInterface->pushVariant(L, var);

	return m_scriptInterface->callFunction(2);
}

WeaponMelee::WeaponMelee(LuaScriptInterface* _interface) :
	Weapon(_interface)
{
	params.blockedByArmor = true;
	params.blockedByShield = true;
	params.combatType = COMBAT_PHYSICALDAMAGE;
}

bool WeaponMelee::configureWeapon(const ItemType& it)
{
	if (it.abilities) {
		elementType = it.abilities->elementType;
		elementDamage = it.abilities->elementDamage;
		params.isAggressive = true;
		params.useCharges = true;
	} else {
		elementType = COMBAT_NONE;
		elementDamage = 0;
	}

	return Weapon::configureWeapon(it);
}

bool WeaponMelee::useWeapon(Player* player, Item* item, Creature* target) const
{
	int32_t damageModifier = playerWeaponCheck(player, target);
	if (damageModifier == 0) {
		return false;
	}
	return internalUseWeapon(player, item, target, damageModifier);
}

bool WeaponMelee::getSkillType(const Player* player, const Item* item,
                               skills_t& skill, uint32_t& skillpoint) const
{
	if (player->getAddAttackSkill() && player->getLastAttackBlockType() != BLOCK_IMMUNITY) {
		skillpoint = 1;
	} else {
		skillpoint = 0;
	}

	WeaponType_t weaponType = item->getWeaponType();
	switch (weaponType) {
		case WEAPON_SWORD: {
			skill = SKILL_SWORD;
			return true;
		}

		case WEAPON_CLUB: {
			skill = SKILL_CLUB;
			return true;
		}

		case WEAPON_AXE: {
			skill = SKILL_AXE;
			return true;
		}

		default:
			break;
	}
	return false;
}

int32_t WeaponMelee::getElementDamage(const Player* player, const Creature*, const Item* item) const
{
	if (elementType == COMBAT_NONE) {
		return 0;
	}

	int32_t attackSkill = player->getWeaponSkill(item);
	int32_t attackValue = std::max<int32_t>(0, elementDamage);
	float attackFactor = player->getAttackFactor();

	int32_t maxValue = Weapons::getMaxWeaponDamage(player->getLevel(), attackSkill, attackValue, attackFactor);
	return -normal_random(0, static_cast<int32_t>(maxValue * player->getVocation()->meleeDamageMultiplier));
}

int32_t WeaponMelee::getWeaponDamage(const Player* player, const Creature*, const Item* item, bool maxDamage /*= false*/) const
{
	int32_t attackSkill = player->getWeaponSkill(item);
	int32_t attackValue = std::max<int32_t>(0, item->getAttack());
	float attackFactor = player->getAttackFactor();

	int32_t maxValue = static_cast<int32_t>(Weapons::getMaxWeaponDamage(player->getLevel(), attackSkill, attackValue, attackFactor) * player->getVocation()->meleeDamageMultiplier);
	if (maxDamage) {
		return -maxValue;
	}

	return -normal_random(0, maxValue);
}

WeaponDistance::WeaponDistance(LuaScriptInterface* _interface) :
	Weapon(_interface)
{
	hitChance = 0;
	maxHitChance = 0;
	breakChance = 0;
	ammuAttackValue = 0;
	params.blockedByArmor = true;
	params.combatType = COMBAT_PHYSICALDAMAGE;
}

bool WeaponDistance::configureEvent(const pugi::xml_node& node)
{
	if (!Weapon::configureEvent(node)) {
		return false;
	}

	const ItemType& it = Item::items[id];

	//default values
	if (it.ammoType != AMMO_NONE) {
		//hit chance on two-handed weapons is limited to 90%
		maxHitChance = 90;
	} else {
		//one-handed is set to 75%
		maxHitChance = 75;
	}

	if (it.hitChance != 0) {
		hitChance = it.hitChance;
	}

	if (it.maxHitChance != -1) {
		maxHitChance = it.maxHitChance;
	}

	if (it.breakChance != -1) {
		breakChance = it.breakChance;
	}

	if (it.ammoAction != AMMOACTION_NONE) {
		ammoAction = it.ammoAction;
	}
	return true;
}

bool WeaponDistance::configureWeapon(const ItemType& it)
{
	//default values
	if (it.ammoType != AMMO_NONE) {
		//hit chance on two-handed weapons is limited to 90%
		maxHitChance = 90;
	} else {
		//one-handed is set to 75%
		maxHitChance = 75;
	}

	params.distanceEffect = it.shootType;
	range = it.shootRange;
	ammuAttackValue = it.attack;

	if (it.hitChance != 0) {
		hitChance = it.hitChance;
	}

	if (it.maxHitChance > 0) {
		maxHitChance = it.maxHitChance;
	}

	if (it.breakChance > 0) {
		breakChance = it.breakChance;
	}

	if (it.ammoAction != AMMOACTION_NONE) {
		ammoAction = it.ammoAction;
	}

	if (it.abilities) {
		elementType = it.abilities->elementType;
		elementDamage = it.abilities->elementDamage;
		params.isAggressive = true;
		params.useCharges = true;
	} else {
		elementType = COMBAT_NONE;
		elementDamage = 0;
	}

	return Weapon::configureWeapon(it);
}

int32_t WeaponDistance::playerWeaponCheck(Player* player, Creature* target) const
{
	Item* bow = player->getWeapon(true);
	if (bow && bow->getWeaponType() == WEAPON_DISTANCE && bow->getID() != id) {
		const Weapon* weapon = g_weapons->getWeapon(bow);
		if (weapon) {
			return weapon->playerWeaponCheck(player, target);
		}
	}
	return Weapon::playerWeaponCheck(player, target);
}

bool WeaponDistance::useWeapon(Player* player, Item* item, Creature* target) const
{
	int32_t damageModifier = playerWeaponCheck(player, target);
	if (damageModifier == 0) {
		return false;
	}

	int32_t chance;

	if (hitChance == 0) {
		//hit chance is based on distance to target and distance skill
		uint32_t skill = player->getSkill(SKILL_DISTANCE, SKILLVALUE_LEVEL);
		const Position& playerPos = player->getPosition();
		const Position& targetPos = target->getPosition();
		uint32_t distance = std::max<uint32_t>(Position::getDistanceX(playerPos, targetPos), Position::getDistanceY(playerPos, targetPos));

		if (maxHitChance == 75) {
			//chance for one-handed weapons
			switch (distance) {
				case 1:
				case 5:
					chance = std::min<uint32_t>(skill, 74) + 1;
					break;
				case 2:
					chance = static_cast<uint32_t>(std::min<uint32_t>(skill, 28) * 2.40f) + 8;
					break;
				case 3:
					chance = static_cast<uint32_t>(std::min<uint32_t>(skill, 45) * 1.55f) + 6;
					break;
				case 4:
					chance = static_cast<uint32_t>(std::min<uint32_t>(skill, 58) * 1.25f) + 3;
					break;
				case 6:
					chance = static_cast<uint32_t>(std::min<uint32_t>(skill, 90) * 0.80f) + 3;
					break;
				case 7:
					chance = static_cast<uint32_t>(std::min<uint32_t>(skill, 104) * 0.70f) + 2;
					break;
				default:
					chance = hitChance;
					break;
			}
		} else if (maxHitChance == 90) {
			//formula for two-handed weapons
			switch (distance) {
				case 1:
				case 5:
					chance = static_cast<uint32_t>(std::min<uint32_t>(skill, 74) * 1.20f) + 1;
					break;
				case 2:
					chance = static_cast<uint32_t>(std::min<uint32_t>(skill, 28) * 3.20f);
					break;
				case 3:
					chance = std::min<uint32_t>(skill, 45) * 2;
					break;
				case 4:
					chance = static_cast<uint32_t>(std::min<uint32_t>(skill, 58) * 1.55f);
					break;
				case 6:
				case 7:
					chance = std::min<uint32_t>(skill, 90);
					break;
				default:
					chance = hitChance;
					break;
			}
		} else if (maxHitChance == 100) {
			switch (distance) {
				case 1:
				case 5:
					chance = static_cast<uint32_t>(std::min<uint32_t>(skill, 73) * 1.35f) + 1;
					break;
				case 2:
					chance = static_cast<uint32_t>(std::min<uint32_t>(skill, 30) * 3.20f) + 4;
					break;
				case 3:
					chance = static_cast<uint32_t>(std::min<uint32_t>(skill, 48) * 2.05f) + 2;
					break;
				case 4:
					chance = static_cast<uint32_t>(std::min<uint32_t>(skill, 65) * 1.50f) + 2;
					break;
				case 6:
					chance = static_cast<uint32_t>(std::min<uint32_t>(skill, 87) * 1.20f) - 4;
					break;
				case 7:
					chance = static_cast<uint32_t>(std::min<uint32_t>(skill, 90) * 1.10f) + 1;
					break;
				default:
					chance = hitChance;
					break;
			}
		} else {
			chance = maxHitChance;
		}
	} else {
		chance = hitChance;
	}

	if (item->getWeaponType() == WEAPON_AMMO) {
		Item* bow = player->getWeapon(true);
		if (bow && bow->getHitChance() != 0) {
			chance += bow->getHitChance();
		}
	}

	if (chance >= uniform_random(1, 100)) {
		Weapon::internalUseWeapon(player, item, target, damageModifier);
	} else {
		//miss target
		Tile* destTile = target->getTile();

		if (!Position::areInRange<1, 1, 0>(player->getPosition(), target->getPosition())) {
			static std::vector<std::pair<int32_t, int32_t>> destList {
				{-1, -1}, {0, -1}, {1, -1},
				{-1,  0}, {0,  0}, {1,  0},
				{-1,  1}, {0,  1}, {1,  1}
			};
			std::shuffle(destList.begin(), destList.end(), getRandomGenerator());

			Position destPos = target->getPosition();

			for (const auto& dir : destList) {
				Tile* tmpTile = g_game.getTile(destPos.x + dir.first, destPos.y + dir.second, destPos.z);

				// Blocking tiles or tiles without ground ain't valid targets for spears
				if (tmpTile && !tmpTile->hasProperty(CONST_PROP_IMMOVABLEBLOCKSOLID) && tmpTile->ground != nullptr) {
					destTile = tmpTile;
					break;
				}
			}
		}

		Weapon::internalUseWeapon(player, item, destTile);
	}
	return true;
}

void WeaponDistance::onUsedAmmo(Item* item, Tile* destTile) const
{
	if (ammoAction == AMMOACTION_MOVEBACK && breakChance > 0 && uniform_random(1, 100) <= breakChance) {
		uint16_t newCount = item->getItemCount();
		if (newCount > 0) {
			newCount--;
		}

		g_game.transformItem(item, item->getID(), newCount);
	} else {
		Weapon::onUsedAmmo(item, destTile);
	}
}

int32_t WeaponDistance::getElementDamage(const Player* player, const Creature* target, const Item* item) const
{
	if (elementType == COMBAT_NONE) {
		return 0;
	}

	int32_t attackValue = elementDamage;
	if (item->getWeaponType() == WEAPON_AMMO) {
		Item* bow = const_cast<Player*>(player)->getWeapon(true);
		if (bow) {
			attackValue += bow->getAttack();
		}
	}

	int32_t attackSkill = player->getSkill(SKILL_DISTANCE, SKILLVALUE_LEVEL);
	float attackFactor = player->getAttackFactor();

	int32_t minValue = 0;
	int32_t maxValue = Weapons::getMaxWeaponDamage(player->getLevel(), attackSkill, attackValue, attackFactor);
	if (target) {
		if (target->getPlayer()) {
			minValue = static_cast<int32_t>(std::ceil(player->getLevel() * 0.1));
		} else {
			minValue = static_cast<int32_t>(std::ceil(player->getLevel() * 0.2));
		}
	}

	return -normal_random(minValue, static_cast<int32_t>(maxValue * player->getVocation()->distDamageMultiplier));
}

int32_t WeaponDistance::getWeaponDamage(const Player* player, const Creature* target, const Item* item, bool maxDamage /*= false*/) const
{
	int32_t attackValue = ammuAttackValue;

	if (item->getWeaponType() == WEAPON_AMMO) {
		Item* bow = const_cast<Player*>(player)->getWeapon(true);
		if (bow) {
			attackValue += bow->getAttack();
		}
	}

	int32_t attackSkill = player->getSkill(SKILL_DISTANCE, SKILLVALUE_LEVEL);
	float attackFactor = player->getAttackFactor();

	int32_t maxValue = static_cast<int32_t>(Weapons::getMaxWeaponDamage(player->getLevel(), attackSkill, attackValue, attackFactor) * player->getVocation()->distDamageMultiplier);
	if (maxDamage) {
		return -maxValue;
	}

	int32_t minValue;
	if (target) {
		if (target->getPlayer()) {
			minValue = static_cast<int32_t>(std::ceil(player->getLevel() * 0.1));
		} else {
			minValue = static_cast<int32_t>(std::ceil(player->getLevel() * 0.2));
		}
	} else {
		minValue = 0;
	}
	return -normal_random(minValue, maxValue);
}

bool WeaponDistance::getSkillType(const Player* player, const Item*, skills_t& skill, uint32_t& skillpoint) const
{
	skill = SKILL_DISTANCE;

	if (player->getAddAttackSkill()) {
		switch (player->getLastAttackBlockType()) {
			case BLOCK_NONE: {
				skillpoint = 2;
				break;
			}

			case BLOCK_DEFENSE:
			case BLOCK_ARMOR: {
				skillpoint = 1;
				break;
			}

			default:
				skillpoint = 0;
				break;
		}
	} else {
		skillpoint = 0;
	}
	return true;
}

WeaponWand::WeaponWand(LuaScriptInterface* _interface) :
	Weapon(_interface)
{
	minChange = 0;
	maxChange = 0;
}

bool WeaponWand::configureEvent(const pugi::xml_node& node)
{
	if (!Weapon::configureEvent(node)) {
		return false;
	}

	pugi::xml_attribute attr;
	if ((attr = node.attribute("min"))) {
		minChange = pugi::cast<int32_t>(attr.value());
	}

	if ((attr = node.attribute("max"))) {
		maxChange = pugi::cast<int32_t>(attr.value());
	}

	if ((attr = node.attribute("type"))) {
		std::string tmpStrValue = asLowerCaseString(attr.as_string());
		if (tmpStrValue == "earth") {
			params.combatType = COMBAT_EARTHDAMAGE;
		} else if (tmpStrValue == "ice") {
			params.combatType = COMBAT_ICEDAMAGE;
		} else if (tmpStrValue == "energy") {
			params.combatType = COMBAT_ENERGYDAMAGE;
		} else if (tmpStrValue == "fire") {
			params.combatType = COMBAT_FIREDAMAGE;
		} else if (tmpStrValue == "death") {
			params.combatType = COMBAT_DEATHDAMAGE;
		} else if (tmpStrValue == "holy") {
			params.combatType = COMBAT_HOLYDAMAGE;
		} else {
			std::cout << "[Warning - WeaponWand::configureEvent] Type \"" << attr.as_string() << "\" does not exist." << std::endl;
		}
	}
	return true;
}

bool WeaponWand::configureWeapon(const ItemType& it)
{
	range = it.shootRange;
	params.distanceEffect = it.shootType;

	return Weapon::configureWeapon(it);
}

int32_t WeaponWand::getWeaponDamage(const Player*, const Creature*, const Item*, bool maxDamage /*= false*/) const
{
	if (maxDamage) {
		return -maxChange;
	}
	return -normal_random(minChange, maxChange);
}
