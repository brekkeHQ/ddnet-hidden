/* (c) Shereef Marzouk. See "licence DDRace.txt" and the readme.txt in the root of the distribution for more information. */
#ifndef GAME_SERVER_GAMEMODES_DDRACE_H
#define GAME_SERVER_GAMEMODES_DDRACE_H

#include <game/server/entities/character.h>
#include <game/server/gamecontroller.h>
#include <game/server/player.h>
#include <game/server/teams.h>

#include <map>
#include <vector>

// Hidden Mode各阶段状态枚举
enum
{

	STEP_S0 = 0, // 大厅
	STEP_S1, // 开始游戏房间
	STEP_S2, // 猎人数量房间
	STEP_S3, // 机器数量房间
	STEP_S4, // 开始游戏房间
	STEP_S5, // 结束游戏，给玩家做win标记。

	NONE_WIN = 0, // 没人赢
	SEEKER_WIN, // 猎人赢
	HIDER_WIN, // 逃生者赢
};

struct CScoreLoadBestTimeResult;
class CGameControllerDDRace : public IGameController
{
public:
	CGameControllerDDRace(class CGameContext *pGameServer);
	~CGameControllerDDRace();

	CScore *Score();

	void OnCharacterSpawn(class CCharacter *pChr) override;
	void HandleCharacterTiles(class CCharacter *pChr, int MapIndex) override;

	void OnPlayerConnect(class CPlayer *pPlayer) override;
	void OnPlayerDisconnect(class CPlayer *pPlayer, const char *pReason) override;

	void OnReset() override;
	void StartRound() override;
	void EndRound() override;
	void Tick() override;

	void DoTeamChange(class CPlayer *pPlayer, int Team, bool DoChatMsg = true) override;

	CClientMask GetMaskForPlayerWorldEvent(int Asker, int ExceptID = -1) override;

	void InitTeleporter();

	int GetPlayerTeam(int ClientID) const;

	CGameTeams m_Teams;

	std::vector<int> unique_random_numbers(int a, int b)
	{
		std::vector<int> result(b);
		std::random_device rd;
		std::mt19937 gen(rd());
		std::uniform_int_distribution<> dis(0, a - 1);
		for(int i = 0; i < b; ++i)
		{
			int r = dis(gen);
			while(std::find(result.begin(), result.end(), r) != result.end())
			{
				r = dis(gen);
			}
			result[i] = r;
		}
		return result;
	}
	static void Teleport(CCharacter *pChr, vec2 Pos); // 传送角色到Pos
	void TeleportPlayerToCheckPoint(CPlayer *pPlayer, int TeleTo); // 传送角色到CP点
	void HiddenStepUpdate(int toStep); // 阶段更新
	void HiddenTick(int nowTick, int endTick, int tickSpeed, int nowStep); // Tick处理
	bool HiddenIsPlayerGameOver(CPlayer *pPlayer); // 判断玩家是否被淘汰

	bool m_HiddenState = false; // Hidden Mode状态
	bool m_KillHammer = true; // 杀人锤子状态

	struct
	{ // Hidden Mode 模式数据
		int nowStep = STEP_S0; // 当前阶段
		int stepDurationTime = -1; // 阶段持续时间 秒计数
		int stepEndTick = -1; // 阶段结束时的Tick
		int whoWin = NONE_WIN; // 哪边赢了

		int machineNum = 0; // 机器数量
		int activedMachine = 0; // 已激活机器数量
		int seekerNum = 0; // 猎人数量

		int iS1PlayerNum = 0; // S1时玩家数量
		int lastActiveClientID = 0; // 最后行动的ClientID

		bool canZoom = true; // 可以缩放、旁观、spec等操作
	} m_Hidden;
	// 重置Hidden Mode各种状态
	void HiddenStateReset()
	{
		m_Hidden.nowStep = STEP_S0;
		m_Hidden.stepDurationTime = -1;
		m_Hidden.stepEndTick = -1;
		m_Hidden.whoWin = NONE_WIN;

		m_Hidden.machineNum = 0;
		m_Hidden.activedMachine = 0;
		m_Hidden.seekerNum = 0;

		m_Hidden.iS1PlayerNum = 0;
		m_Hidden.lastActiveClientID = 0;
		m_Hidden.canZoom = true;
	}
	// 激活设备假人机器
	void HiddenActiveMachine(CPlayer *pPlayer, CPlayer *pMachine)
	{
		if(m_Hidden.nowStep != STEP_S4)
			return;
		char aBuf[256];
		m_Hidden.activedMachine++;
		m_Hidden.stepEndTick = Server()->Tick() + Server()->TickSpeed() * Config()->m_HiddenStepDurationS4;

		// 机器进度，百分比
		double process = (double)m_Hidden.activedMachine / m_Hidden.machineNum * 100;
		str_format(aBuf, sizeof(aBuf), "设备激活进度：%.2f%%", process);
		GameServer()->SendBroadcast(aBuf, -1);
		TeleportPlayerToCheckPoint(pMachine, 242);

		// 添加到最后一次行动
		m_Hidden.lastActiveClientID = pPlayer->GetCID();

		// 击杀MSG提示
		CNetMsg_Sv_KillMsg Msg;
		Msg.m_Killer = pPlayer->GetCID();
		Msg.m_Victim = pMachine->GetCID();
		Msg.m_Weapon = WEAPON_SHOTGUN;
		Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, -1);

		// 声音提示
		GameServer()->CreateSound(pPlayer->GetCharacter()->GetPos(), SOUND_PLAYER_DIE, pPlayer->GetCharacter()->TeamMask());
	}
	// 判断当前地图是否可以启动hidden mode
	bool HiddemModeCanTurnOn()
	{ /* 地图检索判断
			如果地图不是躲猫猫地图则不能开启Hidden Mode
		 */
		bool HasTele201 = !m_TeleOuts[201 - 1].empty();
		bool HasTele211 = !m_TeleOuts[211 - 1].empty();
		bool HasTele221 = !m_TeleOuts[221 - 1].empty();
		bool HasTele231 = !m_TeleOuts[231 - 1].empty();
		bool HasTele241 = !m_TeleOuts[241 - 1].empty();
		bool HasTele251 = !m_TeleOuts[251 - 1].empty();

		if(!HasTele201 ||
			!HasTele211 ||
			!HasTele221 ||
			!HasTele231 ||
			!HasTele241 ||
			!HasTele251)
		{
			return false;
		}

		return true;
	}

	std::map<int, std::vector<vec2>> m_TeleOuts;
	std::map<int, std::vector<vec2>> m_TeleCheckOuts;

	std::shared_ptr<CScoreLoadBestTimeResult> m_pLoadBestTimeResult;
};

#endif // GAME_SERVER_GAMEMODES_DDRACE_H