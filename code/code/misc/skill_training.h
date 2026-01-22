//////////////////////////////////////////////////////////////////////////
//
//  skill_training.h
//
//  Skill training purchase system - allows players to pay gold to increase
//  their skill learnedness at guildmaster NPCs.
//
//////////////////////////////////////////////////////////////////////////

#pragma once

class TBeing;
class TMonster;
class sstring;

// Main entry point (called from GenericGuildMaster)
bool handleSkillTraining(TBeing& ch, TMonster& gm, const sstring& argument);
