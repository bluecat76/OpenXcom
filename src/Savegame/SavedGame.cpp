/*
 * Copyright 2010-2012 OpenXcom Developers.
 *
 * This file is part of OpenXcom.
 *
 * OpenXcom is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * OpenXcom is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with OpenXcom.  If not, see <http://www.gnu.org/licenses/>.
 */
#include "SavedGame.h"
#include <fstream>
#include <sstream>
#include <iomanip>
#include <yaml-cpp/yaml.h>
#include "../Ruleset/Ruleset.h"
#include "../Engine/RNG.h"
#include "../Engine/Language.h"
#include "../Interface/TextList.h"
#include "../Engine/Exception.h"
#include "../Engine/Options.h"
#include "../Engine/CrossPlatform.h"
#include "SavedBattleGame.h"
#include "GameTime.h"
#include "Country.h"
#include "Base.h"
#include "Craft.h"
#include "Region.h"
#include "Ufo.h"
#include "Waypoint.h"
#include "UfopaediaSaved.h"
#include "../Ruleset/RuleResearchProject.h"
#include "ResearchProject.h"
#include "ItemContainer.h"
#include "Soldier.h"
#include "../Ruleset/RuleManufactureInfo.h"
#include "Production.h"

namespace OpenXcom
{
struct findRuleResearchProject : public std::unary_function<ResearchProject *,
								bool>
{
	RuleResearchProject * _toFind;
	findRuleResearchProject(RuleResearchProject * toFind);
	bool operator()(const ResearchProject *r) const;
};

findRuleResearchProject::findRuleResearchProject(RuleResearchProject * toFind) : _toFind(toFind)
{
}

bool findRuleResearchProject::operator()(const ResearchProject *r) const
{
	return _toFind == r->getRuleResearchProject();
}

struct equalProduction : public std::unary_function<Production *,
							bool>
{
	RuleManufactureInfo * _item;
	equalProduction(RuleManufactureInfo * item);
	bool operator()(const Production * p) const;
};

equalProduction::equalProduction(RuleManufactureInfo * item) : _item(item)
{
}

bool equalProduction::operator()(const Production * p) const
{
	return p->getRuleManufactureInfo() == _item;
}

/**
 * Initializes a brand new saved game according to the specified difficulty.
 * @param difficulty Game difficulty.
 */
SavedGame::SavedGame(GameDifficulty difficulty) : _difficulty(difficulty), _funds(0), _countries(), _regions(), _bases(), _ufos(), _craftId(), _waypoints(), _ufoId(1), _waypointId(1), _soldierId(1), _battleGame(0)
{
	RNG::init();
	_time = new GameTime(6, 1, 1, 1999, 12, 0, 0);
	_ufopaedia = new UfopaediaSaved();
}

/**
 * Deletes the game content from memory.
 */
SavedGame::~SavedGame()
{
	delete _time;
	for (std::vector<Country*>::iterator i = _countries.begin(); i != _countries.end(); ++i)
	{
		delete *i;
	}
	for (std::vector<Region*>::iterator i = _regions.begin(); i != _regions.end(); ++i)
	{
		delete *i;
	}
	for (std::vector<Base*>::iterator i = _bases.begin(); i != _bases.end(); ++i)
	{
		delete *i;
	}
	for (std::vector<Ufo*>::iterator i = _ufos.begin(); i != _ufos.end(); ++i)
	{
		delete *i;
	}
	for (std::vector<Waypoint*>::iterator i = _waypoints.begin(); i != _waypoints.end(); ++i)
	{
		delete *i;
	}
	delete _battleGame;
	delete _ufopaedia;
}

/**
 * Gets all the saves found in the user folder
 * and adds them to a text list.
 * @param list Text list.
 * @param lang Loaded language.
 */
void SavedGame::getList(TextList *list, Language *lang)
{
	std::vector<std::string> saves = CrossPlatform::getFolderContents(Options::getUserFolder(), "sav");

	for (std::vector<std::string>::iterator i = saves.begin(); i != saves.end(); ++i)
	{
		std::string file = (*i);
		std::string fullname = Options::getUserFolder() + file;
		std::ifstream fin(fullname.c_str());
		try
		{
			if (!fin)
			{
				throw Exception("Failed to load savegame");
			}
			YAML::Parser parser(fin);
			YAML::Node doc;

			parser.GetNextDocument(doc);
			GameTime time = GameTime(6, 1, 1, 1999, 12, 0, 0);
			time.load(doc["time"]);
			std::stringstream saveTime;
			std::wstringstream saveDay, saveMonth, saveYear;
			saveTime << time.getHour() << ":" << std::setfill('0') << std::setw(2) << time.getMinute();
			saveDay << time.getDay() << lang->getString(time.getDayString());
			saveMonth << lang->getString(time.getMonthString());
			saveYear << time.getYear();
			list->addRow(5, Language::utf8ToWstr(file.substr(0, file.length()-4)).c_str(), Language::utf8ToWstr(saveTime.str()).c_str(), saveDay.str().c_str(), saveMonth.str().c_str(), saveYear.str().c_str());
			fin.close();
		}
		catch (Exception &e)
		{
			std::cerr << e.what() << std::endl;
			continue;
		}
		catch (YAML::Exception &e)
		{
			std::cerr << e.what() << std::endl;
			continue;
		}
	}
}

/**
 * Loads a saved game's contents from a YAML file.
 * @note Assumes the saved game is blank.
 * @param filename YAML filename.
 * @param rule Ruleset for the saved game.
 */
void SavedGame::load(const std::string &filename, Ruleset *rule)
{
	std::string s = Options::getUserFolder() + filename + ".sav";
	std::ifstream fin(s.c_str());
	if (!fin)
	{
		throw Exception("Failed to load savegame");
	}
	YAML::Parser parser(fin);
	YAML::Node doc;

	// Get brief save info
	parser.GetNextDocument(doc);
	std::string v;
	doc["version"] >> v;
	if (v != Options::getVersion())
	{
		throw Exception("Version mismatch");
	}
	_time->load(doc["time"]);

	// Get full save data
	parser.GetNextDocument(doc);
	int a = 0;
	doc["difficulty"] >> a;
	_difficulty = (GameDifficulty)a;
	doc["funds"] >> _funds;

	for (YAML::Iterator i = doc["countries"].begin(); i != doc["countries"].end(); ++i)
	{
		std::string type;
		(*i)["type"] >> type;
		Country *c = new Country(rule->getCountry(type), false);
		c->load(*i);
		_countries.push_back(c);
	}

	for (YAML::Iterator i = doc["regions"].begin(); i != doc["regions"].end(); ++i)
	{
		std::string type;
		(*i)["type"] >> type;
		Region *r = new Region(rule->getRegion(type));
		r->load(*i);
		_regions.push_back(r);
	}

	for (YAML::Iterator i = doc["ufos"].begin(); i != doc["ufos"].end(); ++i)
	{
		std::string type;
		(*i)["type"] >> type;
		Ufo *u = new Ufo(rule->getUfo(type));
		u->load(*i);
		_ufos.push_back(u);
	}

	doc["craftId"] >> _craftId;

	for (YAML::Iterator i = doc["waypoints"].begin(); i != doc["waypoints"].end(); ++i)
	{
		Waypoint *w = new Waypoint();
		w->load(*i);
		_waypoints.push_back(w);
	}

	doc["ufoId"] >> _ufoId;
	doc["waypointId"] >> _waypointId;
	doc["soldierId"] >> _soldierId;

	for (YAML::Iterator i = doc["bases"].begin(); i != doc["bases"].end(); ++i)
	{
		Base *b = new Base(rule);
		b->load(*i, this);
		_bases.push_back(b);
	}

	for(YAML::Iterator it=doc["discovered"].begin();it!=doc["discovered"].end();++it)
	{
		std::string research;
		*it >> research;
		_discovered.push_back(rule->getResearchProject(research));
	}

	if (const YAML::Node *pName = doc.FindValue("battleGame"))
	{
		_battleGame = new SavedBattleGame();
		_battleGame->load(*pName, rule, this);
	}

	if (const YAML::Node *pName = doc.FindValue("ufopaedia"))
	{
		_ufopaedia->load(*pName, rule);
	}
	fin.close();
}

/**
 * Saves a saved game's contents to a YAML file.
 * @param filename YAML filename.
 */
void SavedGame::save(const std::string &filename) const
{
	std::string s = Options::getUserFolder() + filename + ".sav";
	std::ofstream sav(s.c_str());
	if (!sav)
	{
		throw Exception("Failed to save savegame");
	}

	YAML::Emitter out;

	// Saves the brief game info used in the saves list
	out << YAML::BeginDoc;
	out << YAML::BeginMap;
	out << YAML::Key << "version" << YAML::Value << Options::getVersion();
	out << YAML::Key << "time" << YAML::Value;
	_time->save(out);
	out << YAML::EndMap;

	// Saves the full game data to the save
	out << YAML::BeginDoc;
	out << YAML::BeginMap;
	out << YAML::Key << "difficulty" << YAML::Value << _difficulty;
	out << YAML::Key << "funds" << YAML::Value << _funds;
	out << YAML::Key << "countries" << YAML::Value;
	out << YAML::BeginSeq;
	for (std::vector<Country*>::const_iterator i = _countries.begin(); i != _countries.end(); ++i)
	{
		(*i)->save(out);
	}
	out << YAML::EndSeq;
	out << YAML::Key << "regions" << YAML::Value;
	out << YAML::BeginSeq;
	for (std::vector<Region*>::const_iterator i = _regions.begin(); i != _regions.end(); ++i)
	{
		(*i)->save(out);
	}
	out << YAML::EndSeq;
	out << YAML::Key << "bases" << YAML::Value;
	out << YAML::BeginSeq;
	for (std::vector<Base*>::const_iterator i = _bases.begin(); i != _bases.end(); ++i)
	{
		(*i)->save(out);
	}
	out << YAML::EndSeq;
	out << YAML::Key << "ufos" << YAML::Value;
	out << YAML::BeginSeq;
	for (std::vector<Ufo*>::const_iterator i = _ufos.begin(); i != _ufos.end(); ++i)
	{
		(*i)->save(out);
	}
	out << YAML::EndSeq;
	out << YAML::Key << "craftId" << YAML::Value << _craftId;
	out << YAML::Key << "waypoints" << YAML::Value;
	out << YAML::BeginSeq;
	for (std::vector<Waypoint*>::const_iterator i = _waypoints.begin(); i != _waypoints.end(); ++i)
	{
		(*i)->save(out);
	}
	out << YAML::EndSeq;
	out << YAML::Key << "discovered" << YAML::Value;
	out << YAML::BeginSeq;
	for (std::vector<const RuleResearchProject *>::const_iterator i = _discovered.begin(); i != _discovered.end(); ++i)
	{
		out << (*i)->getName ();
	}
	out << YAML::EndSeq;
	out << YAML::Key << "ufoId" << YAML::Value << _ufoId;
	out << YAML::Key << "waypointId" << YAML::Value << _waypointId;
	out << YAML::Key << "soldierId" << YAML::Value << _soldierId;
	if (_battleGame != 0)
	{
		out << YAML::Key << "battleGame" << YAML::Value;
		_battleGame->save(out);
	}
	out << YAML::Key << "ufopaedia" << YAML::Value;
	_ufopaedia->save(out);
	out << YAML::EndMap;
	sav << out.c_str();
	sav.close();
}

/**
 * Returns the player's current funds.
 * @return Current funds.
 */
int SavedGame::getFunds() const
{
	return _funds;
}

/**
 * Changes the player's funds to a new value.
 * @param funds New funds.
 */
void SavedGame::setFunds(int funds)
{
	_funds = funds;
}

/**
 * Gives the player his monthly funds, taking in account
 * all maintenance and profit costs.
 */
void SavedGame::monthlyFunding()
{
	_funds += getCountryFunding() - getBaseMaintenance();
}

/**
 * Returns the current time of the game.
 * @return Pointer to the game time.
 */
GameTime *const SavedGame::getTime() const
{
	return _time;
}

/**
 * Returns the list of countries in the game world.
 * @return Pointer to country list.
 */
std::vector<Country*> *const SavedGame::getCountries()
{
	return &_countries;
}

/**
 * Adds up the monthly funding of all the countries.
 * @return Total funding.
 */
int SavedGame::getCountryFunding() const
{
	int total = 0;
	for (std::vector<Country*>::const_iterator i = _countries.begin(); i != _countries.end(); ++i)
	{
		total += (*i)->getFunding();
	}
	return total;
}

/**
 * Returns the list of world regions.
 * @return Pointer to region list.
 */
std::vector<Region*> *const SavedGame::getRegions()
{
	return &_regions;
}

/**
 * Returns the list of player bases.
 * @return Pointer to base list.
 */
std::vector<Base*> *const SavedGame::getBases()
{
	return &_bases;
}

/**
 * Adds up the monthly maintenance of all the bases.
 * @return Total maintenance.
 */
int SavedGame::getBaseMaintenance() const
{
	int total = 0;
	for (std::vector<Base*>::const_iterator i = _bases.begin(); i != _bases.end(); ++i)
	{
		total += (*i)->getMonthlyMaintenace();
	}
	return total;
}

/**
 * Returns the latest craft IDs for each type.
 * @return Pointer to ID list.
 */
std::map<std::string, int> *const SavedGame::getCraftIds()
{
	return &_craftId;
}

/**
 * Returns the list of alien UFOs.
 * @return Pointer to UFO list.
 */
std::vector<Ufo*> *const SavedGame::getUfos()
{
	return &_ufos;
}

/**
 * Returns the latest ufo ID.
 * @return Pointer to ID value.
 */
int *const SavedGame::getUfoId()
{
	return &_ufoId;
}

/**
 * Returns the latest waypoint ID.
 * @return Pointer to ID value.
 */
int *const SavedGame::getWaypointId()
{
	return &_waypointId;
}

/**
 * Returns the list of craft waypoints.
 * @return Pointer to waypoint list.
 */
std::vector<Waypoint*> *const SavedGame::getWaypoints()
{
	return &_waypoints;
}

/**
 * Get pointer to the battleGame object.
 * @return Pointer to the battleGame object.
 */
SavedBattleGame *const SavedGame::getBattleGame()
{
	return _battleGame;
}

/**
 * Set battleGame object.
 * @param battleGame Pointer to the battleGame object.
 */
void SavedGame::setBattleGame(SavedBattleGame *battleGame)
{
	delete _battleGame;
	_battleGame = battleGame;
}

/**
 * Get pointer to the ufopaedia object.
 * @return Pointer to the ufopaedia object.
 */
UfopaediaSaved *SavedGame::getUfopaedia()
{
	return _ufopaedia;
}

/**
 * Add a ResearchProject to the list of already discovered ResearchProject
 * @param r The newly found ResearchProject
*/
void SavedGame::addFinishedResearch (const RuleResearchProject * r, Ruleset * ruleset)
{
	_discovered.push_back(r);
	if(ruleset)
	{
		std::vector<RuleResearchProject*> availableResearch;
		for(std::vector<Base*>::const_iterator it = _bases.begin (); it != _bases.end (); ++it)
		{
			getDependableResearchBasic(availableResearch, r, ruleset, *it);
		}
		for(std::vector<RuleResearchProject*>::iterator it = availableResearch.begin (); it != availableResearch.end (); ++it)
		{
			if((*it)->getCost() == 0)
			{
				addFinishedResearch(*it, ruleset);
			}
		}
	}
}

/**
   Returns the list of already discovered ResearchProject
 * @return the list of already discovered ResearchProject
*/
const std::vector<const RuleResearchProject *> & SavedGame::getDiscoveredResearchs() const
{
	return _discovered;
}

/**
   Get the list of RuleResearchProject which can be researched in a Base.
   * @param projects the list of ResearchProject which are available.
   * @param ruleset the Game Ruleset
   * @param base a pointer to a Base
*/
void SavedGame::getAvailableResearchProjects (std::vector<RuleResearchProject *> & projects, Ruleset * ruleset, Base * base) const
{
	const std::vector<const RuleResearchProject *> & discovereds(getDiscoveredResearchs());
	const std::map<std::string, RuleResearchProject *> & researchProjects = ruleset->getResearchProjects();
	const std::vector<ResearchProject *> & baseResearchProjects = base->getResearch();
	std::vector<const RuleResearchProject *> unlockeds;
	for(std::vector<const RuleResearchProject *>::const_iterator it = discovereds.begin (); it != discovereds.end (); ++it)
	{
		for(std::vector<RuleResearchProject *>::const_iterator itUnlocked = (*it)->getUnlocked ().begin (); itUnlocked != (*it)->getUnlocked ().end (); ++itUnlocked)
		{
			unlockeds.push_back(*itUnlocked);
		}
	}
	for(std::map<std::string, RuleResearchProject *>::const_iterator iter = researchProjects.begin (); iter != researchProjects.end (); ++iter)
	{
		if (!isResearchAvailable(iter->second, unlockeds))
		{
			continue;
		}
		std::vector<const RuleResearchProject *>::const_iterator itDiscovered = std::find(discovereds.begin (), discovereds.end (), iter->second);
		if (itDiscovered != discovereds.end ())
		{
			continue;
		}
		if (std::find_if (baseResearchProjects.begin(), baseResearchProjects.end (), findRuleResearchProject(iter->second)) != baseResearchProjects.end ())
		{
			continue;
		}
		if (iter->second->needItem() && base->getItems()->getItem(iter->second->getName ()) == 0)
		{
			continue;
		}
		projects.push_back (iter->second);
	}
}

/**
   Get the list of RuleManufactureInfo which can be manufacture in a Base.
   * @param productions the list of Productions which are available.
   * @param ruleset the Game Ruleset
   * @param base a pointer to a Base
*/
void SavedGame::getAvailableProductions (std::vector<RuleManufactureInfo *> & productions, Ruleset * ruleset, Base * base) const
{
	const std::vector<const RuleResearchProject *> & discovereds(getDiscoveredResearchs());
	const std::map<std::string, RuleManufactureInfo *> & items (ruleset->getManufactureProjects ());
	const std::vector<Production *> baseProductions (base->getProductions ());

	for(std::map<std::string, RuleManufactureInfo *>::const_iterator iter = items.begin ();
		iter != items.end ();
		++iter)
	{
		if(std::find(discovereds.begin (), discovereds.end (), ruleset->getResearchProject(iter->first)) == discovereds.end ())
		{
		 	continue;
		}
		if(std::find_if(baseProductions.begin (), baseProductions.end (), equalProduction(iter->second)) != baseProductions.end ())
		{
			continue;
		}
		productions.push_back(iter->second);
	}
}

/**
   Check whether a ResearchProject can be researched.
   * @param r the RuleResearchProject to test.
   * @param unlockeds the list of currently unlocked RuleResearchProject
   * @return true if the RuleResearchProject can be researched
*/
bool SavedGame::isResearchAvailable (RuleResearchProject * r, const std::vector<const RuleResearchProject *> & unlockeds) const
{
	std::vector<RuleResearchProject *>::const_iterator iter = r->getDependencys().begin ();
	const std::vector<const RuleResearchProject *> & discovereds(getDiscoveredResearchs());
	if(std::find(unlockeds.begin (), unlockeds.end (),
			 r) != unlockeds.end ())
	{
		return true;
	}
	while (iter != r->getDependencys().end ())
	{
		std::vector<const RuleResearchProject *>::const_iterator itDiscovered = std::find(discovereds.begin (), discovereds.end (), *iter);
		if (itDiscovered == discovereds.end ())
		{
			return false;
		}
		++iter;
	}

	return true;
}

/**
   Get the list of newly available research projects once a ResearchProject has been completed. This function check for fake ResearchProject.
   * @param dependables the list of RuleResearchProject which are now available.
   * @param research The RuleResearchProject which has just been discovered
   * @param ruleset the Game Ruleset
   * @param base a pointer to a Base
*/
void SavedGame::getDependableResearch (std::vector<RuleResearchProject *> & dependables, const RuleResearchProject *research, Ruleset * ruleset, Base * base) const
{
	getDependableResearchBasic(dependables, research, ruleset, base);
	for(std::vector<const RuleResearchProject *>::const_iterator iter = _discovered.begin (); iter != _discovered.end (); ++iter)
	{
		if((*iter)->getCost() == 0)
		{
			if (std::find((*iter)->getDependencys().begin (), (*iter)->getDependencys().end (), research) != (*iter)->getDependencys().end ())
			{
				getDependableResearchBasic(dependables, *iter, ruleset, base);
			}
		}
	}
}

/**
   Get the list of newly available research projects once a ResearchProject has been completed. This function doesn't check for fake ResearchProject.
   * @param dependables the list of RuleResearchProject which are now available.
   * @param research The RuleResearchProject which has just been discovered
   * @param ruleset the Game Ruleset
   * @param base a pointer to a Base
*/
void SavedGame::getDependableResearchBasic (std::vector<RuleResearchProject *> & dependables, const RuleResearchProject *research, Ruleset * ruleset, Base * base) const
{
	std::vector<RuleResearchProject *> possibleProjects;
	getAvailableResearchProjects(possibleProjects, ruleset, base);
	for(std::vector<RuleResearchProject *>::iterator iter = possibleProjects.begin (); iter != possibleProjects.end (); ++iter)
	{
		if (std::find((*iter)->getDependencys().begin (), (*iter)->getDependencys().end (), research) != (*iter)->getDependencys().end ()
			||
			std::find((*iter)->getUnlocked().begin (), (*iter)->getUnlocked().end (), research) != (*iter)->getUnlocked().end ()
			)
		{
				dependables.push_back(*iter);
			if ((*iter)->getCost() == 0)
			{
				getDependableResearchBasic(dependables, *iter, ruleset, base);
			}
			else
			{
			}
		}
	}
}

/**
 * Returns if a certain research has been completed.
 * @param research Research ID.
 * @return Whether it's researched or not.
 */
bool SavedGame::isResearched(const std::string &research) const
{
	if (research.empty())
		return true;
	for (std::vector<const RuleResearchProject *>::const_iterator i = _discovered.begin(); i != _discovered.end(); ++i)
	{
		if ((*i)->getName() == research)
			return true;
	}

	return false;
}

/**
 * Returns the latest soldier ID.
 * @return Pointer to ID value.
 */
int *const SavedGame::getSoldierId()
{
	return &_soldierId;
}

/**
 * Returns pointer to the Soldier given it's unique ID.
 * @param id A soldier's unique id.
 * @return Pointer to Soldier.
 */
Soldier *const SavedGame::getSoldier(int id) const
{
	for (std::vector<Base*>::const_iterator i = _bases.begin(); i != _bases.end(); ++i)
	{
		for (std::vector<Soldier*>::const_iterator j = (*i)->getSoldiers()->begin(); j != (*i)->getSoldiers()->end(); ++j)
		{
			if ((*j)->getId() == id)
			{
				return (*j);
			}
		}
	}
	return 0;
}

/**
 * Handles the higher promotions (not the rookie-squaddie ones).
 * @return Whether or not some promotions happened - to show the promotions screen.
 */
bool SavedGame::handlePromotions()
{
	size_t soldiersPromoted = 0, soldiersTotal = 0;

	for (std::vector<Base*>::iterator i = _bases.begin(); i != _bases.end(); ++i)
	{
		soldiersTotal += (*i)->getSoldiers()->size();
	}
	Soldier *highestRanked = 0;

	// now determine the number of positions we have of each rank,
	// and the soldier with the heighest promotion score of the rank below it

	int filledPositions = 0, filledPositions2 = 0;
	inspectSoldiers(&highestRanked, &filledPositions, RANK_COMMANDER);
	inspectSoldiers(&highestRanked, &filledPositions2, RANK_COLONEL);
	if (filledPositions < 1 && filledPositions2 > 0)
	{
		// only promote one colonel to commander
		highestRanked->promoteRank();
		soldiersPromoted++;
	}
	inspectSoldiers(&highestRanked, &filledPositions, RANK_COLONEL);
	inspectSoldiers(&highestRanked, &filledPositions2, RANK_CAPTAIN);
	if (filledPositions < (soldiersTotal / 23) && filledPositions2 > 0)
	{
		highestRanked->promoteRank();
		soldiersPromoted++;
	}
	inspectSoldiers(&highestRanked, &filledPositions, RANK_CAPTAIN);
	inspectSoldiers(&highestRanked, &filledPositions2, RANK_SERGEANT);
	if (filledPositions < (soldiersTotal / 11) && filledPositions2 > 0)
	{
		highestRanked->promoteRank();
		soldiersPromoted++;
	}
	inspectSoldiers(&highestRanked, &filledPositions, RANK_SERGEANT);
	inspectSoldiers(&highestRanked, &filledPositions2, RANK_SQUADDIE);
	if (filledPositions < (soldiersTotal / 5) && filledPositions2 > 0)
	{
		highestRanked->promoteRank();
		soldiersPromoted++;
	}

	return (soldiersPromoted > 0);
}

/**
 * Checks how many soldiers of a rank exist and which one has the highest score.
 * @param Pointer to an int to store the total in.
 * @param Rank to inspect.
 * @return The highest scoring soldier of that rank.
 */
void SavedGame::inspectSoldiers(Soldier **highestRanked, int *total, int rank)
{
	int highestScore = 0;
	*total = 0;

	for (std::vector<Base*>::iterator i = _bases.begin(); i != _bases.end(); ++i)
	{
		for (std::vector<Soldier*>::iterator j = (*i)->getSoldiers()->begin(); j != (*i)->getSoldiers()->end(); ++j)
		{
			if ((*j)->getRank() == (SoldierRank)rank)
			{
				(*total)++;
				UnitStats *s = (*j)->getCurrentStats();
				int v1 = 2 * s->health + 2 * s->stamina + 4 * s->reactions + 4 * s->bravery;
				int v2 = v1 + 3*( s->tu + 2*( s->firing ) );
				int v3 = v2 + s->melee + s->throwing + s->strength;
				//if (PsiSkill>0) c3 += PsiStrength + 2* PsiSkill
				int score = v3 + 10 * ( (*j)->getMissions() + (*j)->getKills() );
				if (score > highestScore)
				{
					highestScore = score;
					*highestRanked = (*j);
				}
			}
		}
	}
}

}
