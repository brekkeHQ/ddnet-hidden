/* (c) Shereef Marzouk. See "licence DDRace.txt" and the readme.txt in the root of the distribution for more information. */
/* Based on Race mod stuff and tweaked by GreYFoX@GTi and others to fit our DDRace needs. */
#include "DDRace.h"

#include <engine/server.h>
#include <engine/server/server.h>
#include <engine/shared/config.h>
#include <game/mapitems.h>
#include <game/server/entities/character.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>
#include <game/server/score.h>
#include <game/version.h>

#include "algorithm"
#include "random"
#include "vector"

#define GAME_TYPE_NAME "HiddenWorld"
#define TEST_TYPE_NAME "TestHidden"

CGameControllerDDRace::CGameControllerDDRace(class CGameContext *pGameServer) :
	IGameController(pGameServer), m_Teams(pGameServer), m_pLoadBestTimeResult(nullptr)
{
	m_pGameType = g_Config.m_SvTestingCommands ? TEST_TYPE_NAME : GAME_TYPE_NAME;

	InitTeleporter();
}

CGameControllerDDRace::~CGameControllerDDRace() = default;

CScore *CGameControllerDDRace::Score()
{
	return GameServer()->Score();
}

void CGameControllerDDRace::OnCharacterSpawn(CCharacter *pChr)
{
	IGameController::OnCharacterSpawn(pChr);
	pChr->SetTeams(&m_Teams);
	pChr->SetTeleports(&m_TeleOuts, &m_TeleCheckOuts);
	m_Teams.OnCharacterSpawn(pChr->GetPlayer()->GetCID());
}

void CGameControllerDDRace::HandleCharacterTiles(CCharacter *pChr, int MapIndex)
{
	CPlayer *pPlayer = pChr->GetPlayer();
	const int ClientID = pPlayer->GetCID();

	int m_TileIndex = GameServer()->Collision()->GetTileIndex(MapIndex);
	int m_TileFIndex = GameServer()->Collision()->GetFTileIndex(MapIndex);

	// Sensitivity
	int S1 = GameServer()->Collision()->GetPureMapIndex(vec2(pChr->GetPos().x + pChr->GetProximityRadius() / 3.f, pChr->GetPos().y - pChr->GetProximityRadius() / 3.f));
	int S2 = GameServer()->Collision()->GetPureMapIndex(vec2(pChr->GetPos().x + pChr->GetProximityRadius() / 3.f, pChr->GetPos().y + pChr->GetProximityRadius() / 3.f));
	int S3 = GameServer()->Collision()->GetPureMapIndex(vec2(pChr->GetPos().x - pChr->GetProximityRadius() / 3.f, pChr->GetPos().y - pChr->GetProximityRadius() / 3.f));
	int S4 = GameServer()->Collision()->GetPureMapIndex(vec2(pChr->GetPos().x - pChr->GetProximityRadius() / 3.f, pChr->GetPos().y + pChr->GetProximityRadius() / 3.f));
	int Tile1 = GameServer()->Collision()->GetTileIndex(S1);
	int Tile2 = GameServer()->Collision()->GetTileIndex(S2);
	int Tile3 = GameServer()->Collision()->GetTileIndex(S3);
	int Tile4 = GameServer()->Collision()->GetTileIndex(S4);
	int FTile1 = GameServer()->Collision()->GetFTileIndex(S1);
	int FTile2 = GameServer()->Collision()->GetFTileIndex(S2);
	int FTile3 = GameServer()->Collision()->GetFTileIndex(S3);
	int FTile4 = GameServer()->Collision()->GetFTileIndex(S4);

	const int PlayerDDRaceState = pChr->m_DDRaceState;
	bool IsOnStartTile = (m_TileIndex == TILE_START) || (m_TileFIndex == TILE_START) || FTile1 == TILE_START || FTile2 == TILE_START || FTile3 == TILE_START || FTile4 == TILE_START || Tile1 == TILE_START || Tile2 == TILE_START || Tile3 == TILE_START || Tile4 == TILE_START;
	// start
	if(IsOnStartTile && PlayerDDRaceState != DDRACE_CHEAT)
	{
		const int Team = GetPlayerTeam(ClientID);
		if(m_Teams.GetSaving(Team))
		{
			GameServer()->SendStartWarning(ClientID, "You can't start while loading/saving of team is in progress");
			pChr->Die(ClientID, WEAPON_WORLD);
			return;
		}
		if(g_Config.m_SvTeam == SV_TEAM_MANDATORY && (Team == TEAM_FLOCK || m_Teams.Count(Team) <= 1))
		{
			GameServer()->SendStartWarning(ClientID, "You have to be in a team with other tees to start");
			pChr->Die(ClientID, WEAPON_WORLD);
			return;
		}
		if(g_Config.m_SvTeam != SV_TEAM_FORCED_SOLO && Team > TEAM_FLOCK && Team < TEAM_SUPER && m_Teams.Count(Team) < g_Config.m_SvMinTeamSize)
		{
			char aBuf[128];
			str_format(aBuf, sizeof(aBuf), "Your team has fewer than %d players, so your team rank won't count", g_Config.m_SvMinTeamSize);
			GameServer()->SendStartWarning(ClientID, aBuf);
		}
		if(g_Config.m_SvResetPickups)
		{
			pChr->ResetPickups();
		}

		m_Teams.OnCharacterStart(ClientID);
		pChr->m_LastTimeCp = -1;
		pChr->m_LastTimeCpBroadcasted = -1;
		for(float &CurrentTimeCp : pChr->m_aCurrentTimeCp)
		{
			CurrentTimeCp = 0.0f;
		}
	}

	// finish
	if(((m_TileIndex == TILE_FINISH) || (m_TileFIndex == TILE_FINISH) || FTile1 == TILE_FINISH || FTile2 == TILE_FINISH || FTile3 == TILE_FINISH || FTile4 == TILE_FINISH || Tile1 == TILE_FINISH || Tile2 == TILE_FINISH || Tile3 == TILE_FINISH || Tile4 == TILE_FINISH) && PlayerDDRaceState == DDRACE_STARTED)
		m_Teams.OnCharacterFinish(ClientID);

	// unlock team
	else if(((m_TileIndex == TILE_UNLOCK_TEAM) || (m_TileFIndex == TILE_UNLOCK_TEAM)) && m_Teams.TeamLocked(GetPlayerTeam(ClientID)))
	{
		m_Teams.SetTeamLock(GetPlayerTeam(ClientID), false);
		GameServer()->SendChatTeam(GetPlayerTeam(ClientID), "Your team was unlocked by an unlock team tile");
	}

	// solo part
	if(((m_TileIndex == TILE_SOLO_ENABLE) || (m_TileFIndex == TILE_SOLO_ENABLE)) && !m_Teams.m_Core.GetSolo(ClientID))
	{
		GameServer()->SendChatTarget(ClientID, "You are now in a solo part");
		pChr->SetSolo(true);
	}
	else if(((m_TileIndex == TILE_SOLO_DISABLE) || (m_TileFIndex == TILE_SOLO_DISABLE)) && m_Teams.m_Core.GetSolo(ClientID))
	{
		GameServer()->SendChatTarget(ClientID, "You are now out of the solo part");
		pChr->SetSolo(false);
	}
}

// 玩家进入服务器
void CGameControllerDDRace::OnPlayerConnect(CPlayer *pPlayer)
{
	IGameController::OnPlayerConnect(pPlayer);
	int ClientID = pPlayer->GetCID();

	// init the player
	Score()->PlayerData(ClientID)->Reset();

	// Can't set score here as LoadScore() is threaded, run it in
	// LoadScoreThreaded() instead
	Score()->LoadPlayerData(ClientID);

	if(!Server()->ClientPrevIngame(ClientID))
	{
		// 进入消息
		char aBuf[512];
		str_format(aBuf, sizeof(aBuf), "'%s' entered and joined the %s", Server()->ClientName(ClientID), GetTeamName(pPlayer->GetTeam()));
		// GameServer()->SendChat(-1, CGameContext::CHAT_ALL, aBuf, -1, CGameContext::CHAT_SIX);
		GameServer()->SendChatTarget(-1, aBuf);
		GameServer()->SendChatTarget(ClientID, "Hidden Mode 服务器版本: " GAME_VERSION);
	}
}

// 玩家退出服务器
void CGameControllerDDRace::OnPlayerDisconnect(CPlayer *pPlayer, const char *pReason)
{
	int ClientID = pPlayer->GetCID();
	bool WasModerator = pPlayer->m_Moderating && Server()->ClientIngame(ClientID);

	IGameController::OnPlayerDisconnect(pPlayer, pReason);

	if(!GameServer()->PlayerModerating() && WasModerator)
		GameServer()->SendChat(-1, CGameContext::CHAT_ALL, "Server kick/spec votes are no longer actively moderated.");

	if(g_Config.m_SvTeam != SV_TEAM_FORCED_SOLO)
		m_Teams.SetForceCharacterTeam(ClientID, TEAM_FLOCK);
}

void CGameControllerDDRace::OnReset()
{
	IGameController::OnReset();
	m_Teams.Reset();
}

void CGameControllerDDRace::StartRound()
{
	ResetGame();

	m_RoundStartTick = Server()->Tick();
	m_SuddenDeath = 0;
	m_GameOverTick = -1;
	GameServer()->m_World.m_Paused = false;
	m_ForceBalanced = false;
	Server()->DemoRecorder_HandleAutoStart();
	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "start round type='%s' teamplay='%d'", m_pGameType, m_GameFlags & GAMEFLAG_TEAMS);
	GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "game", aBuf);
}

void CGameControllerDDRace::EndRound()
{
	if(m_Warmup) // game can't end when we are running warmup
		return;

	GameServer()->m_World.m_Paused = true;
	m_GameOverTick = Server()->Tick();
	m_SuddenDeath = 0;
}

void CGameControllerDDRace::Tick()
{
	IGameController::Tick();
	m_Teams.ProcessSaveTeam();
	m_Teams.Tick();

	if(m_pLoadBestTimeResult != nullptr && m_pLoadBestTimeResult->m_Completed)
	{
		if(m_pLoadBestTimeResult->m_Success)
		{
			m_CurrentRecord = m_pLoadBestTimeResult->m_CurrentRecord;

			for(int i = 0; i < MAX_CLIENTS; i++)
			{
				if(GameServer()->m_apPlayers[i] && GameServer()->m_apPlayers[i]->GetClientVersion() >= VERSION_DDRACE)
				{
					GameServer()->SendRecord(i);
				}
			}
		}
		m_pLoadBestTimeResult = nullptr;
	} // 以上是原本的代码

	if(HiddemModeCanTurnOn())
	{
		if(m_Hidden.stepEndTick != -1)
		{ // Hidden Mode相关Tick处理
			int nowTick = Server()->Tick();
			int tickSpeed = Server()->TickSpeed();
			int endTick = m_Hidden.stepEndTick;

			HiddenTick(nowTick, endTick, tickSpeed, m_Hidden.nowStep);
		}
	}

	// 假人设备机器保活
	if(Server()->Tick() % 500 == 0) // 机器人每10s进行一次保活操作(50tick * 10)
	{
		CServer *pServer = (CServer *)Server();
		for(int i = 0; i < 4; i++)
		{
			if(!GameServer()->m_apPlayers[0])
				break; // 假人已被踢出，不能继续了
			pServer->m_NetServer.m_aSlots[i].m_Connection.HiddenFreshLastRecvTime();
			pServer->m_NetServer.m_aSlots[i].m_Connection.HiddenFreshFirstSendTime();
		}
	}
}

void CGameControllerDDRace::HiddenTick(int nowTick, int endTick, int tickSpeed, int nowStep)
{
	char aBuf[256];
	CGameControllerDDRace *pController = (CGameControllerDDRace *)GameServer()->m_pController;
	// 本阶段剩余时间
	double remainTime = static_cast<double>(endTick - nowTick) / tickSpeed;
	if(remainTime) // 仅防止未使用变量警告
	{
	}
	// 本阶段剩余Tick
	int remainTick = endTick - nowTick;

	if(remainTick == tickSpeed * 3 || remainTick == tickSpeed * 2 || remainTick == tickSpeed * 1)
	{
		str_format(aBuf, sizeof(aBuf), "还剩: %.2f 秒", remainTime);
		GameServer()->SendChatTarget(-1, aBuf);
	}

	switch(nowStep)
	{
	case STEP_S0: break;
	case STEP_S5:
	{
		if(nowTick == endTick)
		{
			printf(">>> STEP_S5 DONE\n");
			HiddenStepUpdate(STEP_S0);
		}
		break;
	}
	case STEP_S1:
	{
		int vote201Num = 0;
		int vote202Num = 0;

		vec2 tele201Pos = pController->m_TeleOuts[201 - 1][0];
		vec2 tele202Pos = pController->m_TeleOuts[202 - 1][0];

		for(auto &pPlayer : GameServer()->m_apPlayers)
		{
			if(HiddenIsPlayerGameOver(pPlayer))
				continue;
			vec2 vPlayerPos = pPlayer->GetCharacter()->GetPos();
			double distance201 = sqrt(pow(vPlayerPos.x - tele201Pos.x, 2) + pow(vPlayerPos.y - tele201Pos.y, 2));
			double distance202 = sqrt(pow(vPlayerPos.x - tele202Pos.x, 2) + pow(vPlayerPos.y - tele202Pos.y, 2));
			if(distance201 < distance202)
				vote201Num++;
			else
				vote202Num++;
		}

		str_format(aBuf, sizeof(aBuf), "%s:%d \t\t\t %s:%d", Config()->m_HiddenStepVoteS1A, vote201Num, Config()->m_HiddenStepVoteS1B, vote202Num);
		GameServer()->SendBroadcast(aBuf, -1);

		if(nowTick == endTick)
		{ // 选择时间结束，开始判断
			printf(">>> STEP_S1 DONE\n");

			m_Hidden.stepDurationTime = -1;
			if(vote201Num > vote202Num)
			{ // 同意
				HiddenStepUpdate(STEP_S2);
				str_format(aBuf, sizeof(aBuf), "投票结果: 开始");
			}
			else
			{ // 拒绝
				HiddenStepUpdate(STEP_S0);
				str_format(aBuf, sizeof(aBuf), "投票结果: 退出");
			}
			GameServer()->SendChatTarget(-1, aBuf);
		}
		break;
	}
	case STEP_S2:
	{
		int vote211Num = 0;
		int vote212Num = 0;
		int vote213Num = 0;
		int vote214Num = 0;

		vec2 tele211Pos = pController->m_TeleOuts[211 - 1][0];
		vec2 tele212Pos = pController->m_TeleOuts[212 - 1][0];
		vec2 tele213Pos = pController->m_TeleOuts[213 - 1][0];
		vec2 tele214Pos = pController->m_TeleOuts[214 - 1][0];

		for(auto &pPlayer : GameServer()->m_apPlayers)
		{
			if(HiddenIsPlayerGameOver(pPlayer))
				continue;
			vec2 vPlayerPos = pPlayer->GetCharacter()->GetPos();
			double distance211 = sqrt(pow(vPlayerPos.x - tele211Pos.x, 2) + pow(vPlayerPos.y - tele211Pos.y, 2));
			double distance212 = sqrt(pow(vPlayerPos.x - tele212Pos.x, 2) + pow(vPlayerPos.y - tele212Pos.y, 2));
			double distance213 = sqrt(pow(vPlayerPos.x - tele213Pos.x, 2) + pow(vPlayerPos.y - tele213Pos.y, 2));
			double distance214 = sqrt(pow(vPlayerPos.x - tele214Pos.x, 2) + pow(vPlayerPos.y - tele214Pos.y, 2));

			if(distance212 < distance213)
			{ // 更靠近左边
				if(distance211 < distance212)
					vote211Num++;
				else
					vote212Num++;
			}
			else
			{ // 更靠近右边
				if(distance213 < distance214)
					vote213Num++;
				else
					vote214Num++;
			}
		}

		str_format(aBuf, sizeof(aBuf), "%s:%d \t %s:%d \t %s:%d \t %s:%d", Config()->m_HiddenStepVoteS2A, vote211Num, Config()->m_HiddenStepVoteS2B, vote212Num, Config()->m_HiddenStepVoteS2C, vote213Num, Config()->m_HiddenStepVoteS2D, vote214Num);
		GameServer()->SendBroadcast(aBuf, -1);

		if(nowTick == endTick)
		{ // 选择时间结束，开始判断
			printf(">>> STEP_S2 DONE\n");

			m_Hidden.stepDurationTime = -1;
			if(vote213Num > vote212Num && vote213Num > vote214Num)
			{
				m_Hidden.seekerNum = Config()->m_HiddenStepVoteS2CValue;
			}
			else if(vote214Num > vote213Num && vote214Num > vote212Num)
			{
				m_Hidden.seekerNum = Config()->m_HiddenStepVoteS2DValue;
			}
			else
			{
				m_Hidden.seekerNum = Config()->m_HiddenStepVoteS2BValue;
			}
			if(m_Hidden.seekerNum >= m_Hidden.iS1PlayerNum)
			{ // 玩家数量少于选择的猎人数量
				HiddenStepUpdate(STEP_S0);
				GameServer()->SendBroadcast("猎人数量过高", -1);
			}
			else
			{ // 猎人数量正确
				HiddenStepUpdate(STEP_S3);
				str_format(aBuf, sizeof(aBuf), "投票结果: %d", m_Hidden.seekerNum);
				GameServer()->SendChatTarget(-1, aBuf);
			}
		}
		break;
	}
	case STEP_S3:
	{
		int vote221Num = 0;
		int vote222Num = 0;
		int vote223Num = 0;
		int vote224Num = 0;

		vec2 tele221Pos = pController->m_TeleOuts[221 - 1][0];
		vec2 tele222Pos = pController->m_TeleOuts[222 - 1][0];
		vec2 tele223Pos = pController->m_TeleOuts[223 - 1][0];
		vec2 tele224Pos = pController->m_TeleOuts[224 - 1][0];

		for(auto &pPlayer : GameServer()->m_apPlayers)
		{
			if(HiddenIsPlayerGameOver(pPlayer))
				continue;
			vec2 vPlayerPos = pPlayer->GetCharacter()->GetPos();
			double distance211 = sqrt(pow(vPlayerPos.x - tele221Pos.x, 2) + pow(vPlayerPos.y - tele221Pos.y, 2));
			double distance212 = sqrt(pow(vPlayerPos.x - tele222Pos.x, 2) + pow(vPlayerPos.y - tele222Pos.y, 2));
			double distance213 = sqrt(pow(vPlayerPos.x - tele223Pos.x, 2) + pow(vPlayerPos.y - tele223Pos.y, 2));
			double distance214 = sqrt(pow(vPlayerPos.x - tele224Pos.x, 2) + pow(vPlayerPos.y - tele224Pos.y, 2));

			if(distance212 < distance213)
			{ // 更靠近左边
				if(distance211 < distance212)
					vote221Num++;
				else
					vote222Num++;
			}
			else
			{ // 更靠近右边
				if(distance213 < distance214)
					vote223Num++;
				else
					vote224Num++;
			}
		}

		str_format(aBuf, sizeof(aBuf), "%s:%d \t %s:%d \t %s:%d \t %s:%d", Config()->m_HiddenStepVoteS3A, vote221Num, Config()->m_HiddenStepVoteS3B, vote222Num, Config()->m_HiddenStepVoteS3C, vote223Num, Config()->m_HiddenStepVoteS3D, vote224Num);
		GameServer()->SendBroadcast(aBuf, -1);

		if(nowTick == endTick)
		{ // 选择时间结束，开始判断
			printf(">>> STEP_S3 DONE\n");

			m_Hidden.stepDurationTime = -1;
			if(vote223Num > vote222Num && vote223Num > vote224Num)
			{
				m_Hidden.machineNum = Config()->m_HiddenStepVoteS3CValue;
			}
			else if(vote224Num > vote223Num && vote224Num > vote222Num)
			{
				m_Hidden.machineNum = Config()->m_HiddenStepVoteS3DValue;
			}
			else
			{
				m_Hidden.machineNum = Config()->m_HiddenStepVoteS3BValue;
			}
			HiddenStepUpdate(STEP_S4);
			str_format(aBuf, sizeof(aBuf), "投票结果: %d", m_Hidden.machineNum);
			GameServer()->SendChatTarget(-1, aBuf);
		}
		break;
	}
	case STEP_S4:
	{
		for(auto &pPlayer : GameServer()->m_apPlayers)
		{ // 玩家状态更新检测
			if(!pPlayer)
				continue;
			if(pPlayer->m_Hidden.m_IsDummyMachine)
				continue;

			bool isInGame = pPlayer->m_Hidden.m_InGame;
			bool isBeenKilled = pPlayer->m_Hidden.m_HasBeenKilled;
			bool isInSpectator = pPlayer->GetTeam() == TEAM_SPECTATORS;
			bool isLose = pPlayer->m_Hidden.m_IsLose;

			if(isInGame && isBeenKilled && !isInSpectator && !isLose)
			{ // 玩家被猎人锤中
				// 全局广播	受害者出局
				str_format(aBuf, sizeof(aBuf), "%s 出局了!", Server()->ClientName(pPlayer->GetCID()));
				GameServer()->SendBroadcast(aBuf, -1);
				// 聊天消息
				str_format(aBuf, sizeof(aBuf), "%s:游戏结束后复活", Server()->ClientName(pPlayer->GetCID()));
				GameServer()->SendChatTarget(-1, aBuf);
				// 受害者移动到旁观列表
				pPlayer->SetTeam(TEAM_SPECTATORS, false);
				// 该玩家标记为失败
				pPlayer->m_Hidden.m_IsLose = true;
			}
			else if(!isInGame && !isInSpectator)
			{
				// 个人广播	下一轮加入
				str_format(aBuf, sizeof(aBuf), "%s:等待加入", Server()->ClientName(pPlayer->GetCID()));
				GameServer()->SendBroadcast(aBuf, pPlayer->GetCID());
				// 聊天消息
				str_format(aBuf, sizeof(aBuf), "%s:等待加入", Server()->ClientName(pPlayer->GetCID()));
				GameServer()->SendChatTarget(pPlayer->GetCID(), aBuf);
				// 移动到旁观列表
				pPlayer->SetTeam(TEAM_SPECTATORS, false);
			}
		}

		int seekerNum = 0; // 猎人数量
		int hiderNum = 0; // 求生者数量

		for(auto &pPlayer : GameServer()->m_apPlayers)
		{
			if(HiddenIsPlayerGameOver(pPlayer))
				continue;

			if(pPlayer->m_Hidden.m_IsSeeker)
				seekerNum++;
			else
				hiderNum++;
		}

		if(seekerNum == 0 && hiderNum == 0)
		{ // 异常
			GameServer()->SendBroadcast("人数异常，游戏结束", -1);
			HiddenStepUpdate(STEP_S0);
			break;
		}

		if(hiderNum == 0)
		{ // 求生者全死 --> 猎人赢了
			this->m_Hidden.whoWin = SEEKER_WIN;
		}
		else if(seekerNum == 0 || m_Hidden.activedMachine >= m_Hidden.machineNum)
		{ // 机器全部激活 --> 求生者赢了
			this->m_Hidden.whoWin = HIDER_WIN;
		}

		if(m_Hidden.whoWin > NONE_WIN)
		{ // 有一方胜利
			// 游戏结束
			HiddenStepUpdate(STEP_S5);
			printf(">>> STEP_S4 DONE\n");
			break;
		}

		if(nowTick == endTick - tickSpeed * 90 ||
			nowTick == endTick - tickSpeed * 60 ||
			nowTick == endTick - tickSpeed * 30 ||
			nowTick == endTick - tickSpeed * 15 ||
			nowTick == endTick - tickSpeed * 5 ||
			nowTick == endTick - tickSpeed * 3 ||
			nowTick == endTick - tickSpeed * 2 ||
			nowTick == endTick - tickSpeed * 1)
		{ // 机器剩余激活时间提示
			double tipRemainTime = (double)(endTick - nowTick) / tickSpeed;
			str_format(aBuf, sizeof(aBuf), "必须有设备在%.2f秒内激活", tipRemainTime);
			GameServer()->SendBroadcast(aBuf, -1);
		}

		if(nowTick == endTick)
		{ // 求生者没有在规定时间内激活机器
			for(auto &pPlayer : GameServer()->m_apPlayers)
			{ // 所有还在游戏中的求生者设置为被killed状态
				if(!pPlayer)
					continue;

				if(HiddenIsPlayerGameOver(pPlayer))
					continue;
				if(pPlayer->m_Hidden.m_IsSeeker)
					continue;

				pPlayer->m_Hidden.m_HasBeenKilled = true;
			}
		}
		break;
	}
	}
}

/*
	玩家游戏结束了吗？
 */
bool CGameControllerDDRace::HiddenIsPlayerGameOver(CPlayer *pPlayer)
{
	if(!pPlayer)
		return true; // 没有玩家
	if(!pPlayer->GetCharacter())
		return true; // 没有生成角色

	if(pPlayer->GetTeam() == TEAM_SPECTATORS)
		return true; // 在旁观模式

	if(pPlayer->m_Hidden.m_IsDummyMachine)
		return true; // 是机器(假人、设备)
	if(pPlayer->m_Hidden.m_HasBeenKilled)
		return true; // 已被杀死
	if(pPlayer->m_Hidden.m_InGame == false)
		return true; // 未加入游戏
	if(pPlayer->m_Hidden.m_IsLose)
		return true; // 已标记为失败

	return false;
}
void CGameControllerDDRace::HiddenStepUpdate(int toStep)
{
	int tickSpeed = Server()->TickSpeed();
	int tickNow = Server()->Tick();
	switch(toStep)
	{
	case STEP_S0:
	{ // 大厅
		printf("STEP_S0: init setup\n");
		CGameControllerDDRace *pController = (CGameControllerDDRace *)GameServer()->m_pController;
		// 如果没有到S4，则玩家重生
		if(m_Hidden.nowStep < STEP_S4)
		{
			for(auto &pPlayer : GameServer()->m_apPlayers)
			{
				if(!pPlayer)
					continue;
				if(pPlayer->m_Hidden.m_IsDummyMachine)
					continue;

				pPlayer->SetTeam(TEAM_FLOCK, false);
				pPlayer->TryRespawn();
			}
		}
		else
		{ // 大于等于S4 --> 假人(设备、机器)归位
			for(auto &pPlayer : GameServer()->m_apPlayers)
			{
				if(!pPlayer)
					continue;
				if(!pPlayer->m_Hidden.m_IsDummyMachine)
					continue; // 不是假人(设备、机器)

				pController->TeleportPlayerToCheckPoint(pPlayer, 241);
			}
		}

		if(m_Hidden.whoWin != NONE_WIN)
		{ // 冠军房间传送
			for(auto &pPlayer : GameServer()->m_apPlayers)
			{
				if(!pPlayer)
					continue;
				if(pPlayer->m_Hidden.m_IsDummyMachine)
					continue;
				if(pPlayer->m_Hidden.m_InGame == false)
					continue;

				if(pPlayer->m_Hidden.m_IsWin)
					TeleportPlayerToCheckPoint(pPlayer, 251);
				else
					TeleportPlayerToCheckPoint(pPlayer, 252);
			}
		}

		HiddenStateReset();

		// 关闭Hidden Mode
		GameServer()->HiddenModeStop();
		printf("STEP_S0: init done\n");
		break;
	}
	case STEP_S1:
	{ // 玩家传送到S1 开始游戏房间
		printf("STEP_S1: init setup\n");
		CGameControllerDDRace *pController = (CGameControllerDDRace *)GameServer()->m_pController;
		int iPlayerNum = 0;
		for(auto &pPlayer : GameServer()->m_apPlayers)
		{
			if(HiddenIsPlayerGameOver(pPlayer))
				continue;

			pController->TeleportPlayerToCheckPoint(pPlayer, 201);
			iPlayerNum++;
		}
		m_Hidden.nowStep = STEP_S1;
		m_Hidden.stepDurationTime = GameServer()->Config()->m_HiddenStepDurationS1;
		m_Hidden.stepEndTick = tickNow + tickSpeed * m_Hidden.stepDurationTime;
		m_Hidden.iS1PlayerNum = iPlayerNum;

		printf("STEP_S1: init done\n");
		break;
	}
	case STEP_S2:
	{ // 玩家传送到S2 猎人数量房间
		printf("STEP_S2: init setup\n");
		CGameControllerDDRace *pController = (CGameControllerDDRace *)GameServer()->m_pController;
		for(auto &pPlayer : GameServer()->m_apPlayers)
		{
			if(HiddenIsPlayerGameOver(pPlayer))
				continue;
			vec2 tele211Pos = pController->m_TeleOuts[211 - 1][0];
			pController->Teleport(pPlayer->GetCharacter(), tele211Pos);
		}
		m_Hidden.nowStep = STEP_S2;
		m_Hidden.stepDurationTime = GameServer()->Config()->m_HiddenStepDurationS2;
		m_Hidden.stepEndTick = tickNow + tickSpeed * m_Hidden.stepDurationTime;

		printf("STEP_S2: init done\n");
		break;
	}
	case STEP_S3:
	{ // 玩家传送到S3 机器数量房间
		printf("STEP_S3: init setup\n");
		CGameControllerDDRace *pController = (CGameControllerDDRace *)GameServer()->m_pController;
		for(auto &pPlayer : GameServer()->m_apPlayers)
		{
			if(HiddenIsPlayerGameOver(pPlayer))
				continue;
			vec2 tele221Pos = pController->m_TeleOuts[221 - 1][0];
			pController->Teleport(pPlayer->GetCharacter(), tele221Pos);
		}
		m_Hidden.nowStep = STEP_S3;
		m_Hidden.stepDurationTime = GameServer()->Config()->m_HiddenStepDurationS3;
		m_Hidden.stepEndTick = tickNow + tickSpeed * m_Hidden.stepDurationTime;

		printf("STEP_S3: init done\n");
		break;
	}
	case STEP_S4:
	{ // 玩家传送到S4 猎人房间
		printf("STEP_S4: init setup\n");
		CGameControllerDDRace *pController = (CGameControllerDDRace *)GameServer()->m_pController;

		// 随机选取猎人
		std::vector<int> randomVector = unique_random_numbers(m_Hidden.iS1PlayerNum, m_Hidden.seekerNum);
		printf("STEP_S4 猎人分配如下: ");
		int currentIndex = 0;
		for(auto &pPlayer : GameServer()->m_apPlayers)
		{
			if(HiddenIsPlayerGameOver(pPlayer))
				continue;

			for(int i = 0; i < (int)(randomVector.size()); i++)
			{
				if(randomVector[i] == currentIndex)
				{
					pPlayer->m_Hidden.m_IsSeeker = true;
					break;
				}
			}
			// 取消旁观
			pPlayer->Pause(CPlayer::PAUSE_NONE, true);

			if(pPlayer->m_Hidden.m_IsSeeker)
			{ // 玩家是猎人
				pController->TeleportPlayerToCheckPoint(pPlayer, 232);
				printf("%s ", Server()->ClientName(pPlayer->GetCID()));
			}
			else
			{ // 不是猎人
				pController->TeleportPlayerToCheckPoint(pPlayer, 231);
			}

			currentIndex++;
		}
		printf("\n");

		// 机器(假人、设备)开始运作
		for(auto &pPlayer : GameServer()->m_apPlayers)
		{
			if(!pPlayer)
				continue;
			if(!pPlayer->m_Hidden.m_IsDummyMachine)
				continue; // 不是机器人

			pController->TeleportPlayerToCheckPoint(pPlayer, 242);
		}

		// 禁止旁观
		m_Hidden.canZoom = false;

		m_Hidden.nowStep = STEP_S4;
		m_Hidden.stepDurationTime = GameServer()->Config()->m_HiddenStepDurationS4;
		m_Hidden.stepEndTick = tickNow + tickSpeed * m_Hidden.stepDurationTime;

		printf("STEP_S4: init done\n");
		break;
	}
	case STEP_S5:
	{ // 结束游戏
		printf("STEP_S5: init setup\n");
		char aBuf[256];
		CGameControllerDDRace *pController = (CGameControllerDDRace *)GameServer()->m_pController;
		if(pController->m_Hidden.whoWin == SEEKER_WIN)
		{ // 猎人胜利标记
			for(auto &pPlayer : GameServer()->m_apPlayers)
			{
				if(!pPlayer)
					continue;
				if(HiddenIsPlayerGameOver(pPlayer))
					continue; // 不在游戏中

				if(pPlayer->m_Hidden.m_IsSeeker)
					pPlayer->m_Hidden.m_IsWin = true;
			}
			str_copy(aBuf, Config()->m_HiddenSeekerWin);
		}
		else if(pController->m_Hidden.whoWin == HIDER_WIN)
		{ // 求生者胜利标记
			for(auto &pPlayer : GameServer()->m_apPlayers)
			{
				if(!pPlayer)
					continue;
				if(HiddenIsPlayerGameOver(pPlayer))
					continue; // 不在游戏中

				if(!pPlayer->m_Hidden.m_IsSeeker)
					pPlayer->m_Hidden.m_IsWin = true;
			}
			str_copy(aBuf, Config()->m_HiddenHiderWin);
		}

		// 只保留最后一次行动的玩家，其他玩家都旁观他
		for(auto &pPlayer : GameServer()->m_apPlayers)
		{
			if(!pPlayer)
				continue;
			if(pPlayer->m_Hidden.m_IsDummyMachine)
				continue; // 排除机器人
			if(pPlayer->GetCID() == m_Hidden.lastActiveClientID)
				continue; // 排除最后一次行动玩家

			pPlayer->SetTeam(TEAM_SPECTATORS, false);
			pPlayer->m_SpectatorID = m_Hidden.lastActiveClientID;
		}

		// 消息广播
		GameServer()->SendBroadcast(aBuf, -1);

		m_Hidden.nowStep = STEP_S5;
		m_Hidden.stepDurationTime = 4;
		m_Hidden.stepEndTick = tickNow + tickSpeed * m_Hidden.stepDurationTime;

		printf("STEP_S5: init done\n");
		break;
	}
	}
}

void CGameControllerDDRace::TeleportPlayerToCheckPoint(CPlayer *pPlayer, int TeleTo)
{
	if(!pPlayer)
		return;

	CCharacter *pChr = pPlayer->GetCharacter();
	if(!m_TeleOuts[TeleTo - 1].empty())
	{
		int TeleOutIndex = GameServer()->m_World.m_Core.RandomOr0(m_TeleOuts[TeleTo - 1].size());
		if(pChr)
		{
			Teleport(pChr, m_TeleOuts[TeleTo - 1][TeleOutIndex]);
		}
		else
		{
			vec2 TeleOutPos = m_TeleOuts[TeleTo - 1][TeleOutIndex];
			pPlayer->ForceSpawn(TeleOutPos);
		}
	}
}

// 传送角色到Pos
void CGameControllerDDRace::Teleport(CCharacter *pChr, vec2 Pos)
{
	pChr->Core()->m_Pos = Pos;
	pChr->m_Pos = Pos;
	pChr->m_PrevPos = Pos;
}

void CGameControllerDDRace::DoTeamChange(class CPlayer *pPlayer, int Team, bool DoChatMsg)
{
	Team = ClampTeam(Team);
	if(Team == pPlayer->GetTeam())
		return;

	CCharacter *pCharacter = pPlayer->GetCharacter();

	if(Team == TEAM_SPECTATORS)
	{
		if(g_Config.m_SvTeam != SV_TEAM_FORCED_SOLO && pCharacter)
		{
			// Joining spectators should not kill a locked team, but should still
			// check if the team finished by you leaving it.
			int DDRTeam = pCharacter->Team();
			m_Teams.SetForceCharacterTeam(pPlayer->GetCID(), TEAM_FLOCK);
			m_Teams.CheckTeamFinished(DDRTeam);
		}
	}

	IGameController::DoTeamChange(pPlayer, Team, DoChatMsg);
}

CClientMask CGameControllerDDRace::GetMaskForPlayerWorldEvent(int Asker, int ExceptID)
{
	if(Asker == -1)
		return CClientMask().set().reset(ExceptID);

	return m_Teams.TeamMask(GetPlayerTeam(Asker), ExceptID, Asker);
}

void CGameControllerDDRace::InitTeleporter()
{
	if(!GameServer()->Collision()->Layers()->TeleLayer())
		return;
	int Width = GameServer()->Collision()->Layers()->TeleLayer()->m_Width;
	int Height = GameServer()->Collision()->Layers()->TeleLayer()->m_Height;

	for(int i = 0; i < Width * Height; i++)
	{
		int Number = GameServer()->Collision()->TeleLayer()[i].m_Number;
		int Type = GameServer()->Collision()->TeleLayer()[i].m_Type;
		if(Number > 0)
		{
			if(Type == TILE_TELEOUT)
			{
				m_TeleOuts[Number - 1].push_back(
					vec2(i % Width * 32 + 16, i / Width * 32 + 16));
			}
			else if(Type == TILE_TELECHECKOUT)
			{
				m_TeleCheckOuts[Number - 1].push_back(
					vec2(i % Width * 32 + 16, i / Width * 32 + 16));
			}
		}
	}
}

int CGameControllerDDRace::GetPlayerTeam(int ClientID) const
{
	return m_Teams.m_Core.Team(ClientID);
}
