#include "World/PixelQuestTypes.h"

/**
 * The opening quest line of Brookhollow, the village in the middle of PixelTest, in the order it is meant to be played:
 *
 *   FirstSteps (Elder Maren -> Captain Brann)   move, look around, switch the camera
 *     Training (Brann)                           attack, combo, dodge
 *       Smithing (Smith Hilda)                   mine ore; unlocks the spear
 *     Companion (Mage Ilsa)                      companion: deploy, manual casting, bolt, empower
 *   Garden (Herbalist Orlen)                     gather and fight for real (the original quest, open from the start)
 *   SlimeKing (Maren)                            needs Garden + Smithing + Companion: boss fight
 *
 * New enemy kinds plug in through Kill objectives: give the spawned enemy an actor tag (APixelEnemySpawner::EnemyTag)
 * and ask for that tag here.
 *
 * Player-facing text is Korean. Ids (quest, NPC, item, marker) stay English: they are keys in save games and maps.
 * The dialogue box is about 36 Korean characters wide; keep each line (separated by \n) under that.
 */

namespace
{
	FPixelQuestObjective Objective(EPixelObjective Type, const TCHAR* Target, int32 Count, const TCHAR* Text)
	{
		FPixelQuestObjective Result;
		Result.Type = Type;
		Result.Target = Target ? FName(Target) : NAME_None;
		Result.Count = Count;
		Result.Text = Text;
		return Result;
	}

	TArray<FPixelQuestDef> BuildQuests()
	{
		TArray<FPixelQuestDef> Quests;

		{
			FPixelQuestDef& Q = Quests.AddDefaulted_GetRef();
			Q.Id = TEXT("FirstSteps");
			Q.Title = TEXT("첫 걸음");
			Q.Giver = TEXT("Maren");
			Q.TurnIn = TEXT("Brann");
			Q.OfferText = TEXT("브룩홀로우에 온 걸 환영하네, 여행자. 요즘 길이 예전 같지 않아.\n"
				"서쪽 관문까지 걸어가서 주변을 잘 둘러보게 [V].\n"
				"그다음 동쪽 훈련장에 있는 브란 대장을 찾아가게.");
			Q.ProgressText = TEXT("촌장님이 보냈다고? 서쪽 관문부터 둘러보고 오게.");
			Q.CompleteText = TEXT("자네가 촌장님이 말한 그 사람이군. 다리는 튼튼해 보이네.\n"
				"어디, 칼은 제대로 휘두를 줄 아는지 볼까.");
			Q.Objectives = {
				Objective(EPixelObjective::Reach, TEXT("WestGate"), 1, TEXT("서쪽 관문 둘러보기")),
				Objective(EPixelObjective::Action, *PixelQuestActions::ToggleView.ToString(), 1, TEXT("시점 바꾸기 [V]")),
			};
			Q.Reward.Coins = 10;
		}
		{
			FPixelQuestDef& Q = Quests.AddDefaulted_GetRef();
			Q.Id = TEXT("Training");
			Q.Title = TEXT("강철과 짚단");
			Q.Giver = TEXT("Brann");
			Q.TurnIn = TEXT("Brann");
			Q.Requires = { TEXT("FirstSteps") };
			Q.OfferText = TEXT("저 허수아비는 신병 셋을 버텨냈지. 쉬지 말고 계속 베다가\n"
				"연속 공격의 마지막 찌르기까지 넣어 보게. 구르기도 연습하고.\n"
				"슬라임한테 깔리면 아프지만, 피하면 하나도 안 아프다네.");
			Q.ProgressText = TEXT("계속하게. 베고, 베고, 베고, 베고, 찌르기 - 그리고 구르기 [Space].");
			Q.CompleteText = TEXT("첫날치고는 나쁘지 않군. 자, 수고비일세.\n"
				"대장간의 힐다도 곡괭이 쥘 줄 아는 사람을 찾고 있더군.");
			Q.Objectives = {
				Objective(EPixelObjective::Action, *PixelQuestActions::HitDummy.ToString(), 8, TEXT("허수아비 때리기")),
				Objective(EPixelObjective::Action, *PixelQuestActions::ComboFinisher.ToString(), 1, TEXT("5연속 공격 마무리하기")),
				Objective(EPixelObjective::Action, *PixelQuestActions::Dodge.ToString(), 3, TEXT("구르기 [Space]")),
			};
			Q.Reward.Coins = 20;
			Q.Reward.MaxHealth = 10.f;
		}
		{
			FPixelQuestDef& Q = Quests.AddDefaulted_GetRef();
			Q.Id = TEXT("Garden");
			Q.Title = TEXT("짓밟힌 정원");
			Q.Giver = TEXT("Orlen");
			Q.TurnIn = TEXT("Orlen");
			Q.OfferText = TEXT("슬라임들이 자꾸 월광초 밭을 짓밟고 다녀요.\n"
				"월광초 3개를 가져다주고, 슬라임도 3마리만 쫓아내 줄래요?");
			Q.ProgressText = TEXT("아직이에요? 월광초는 제 주변 밭에서 자라요.");
			Q.CompleteText = TEXT("다 모였네요. 정원도 다시 조용해졌고요!\n"
				"이 강장제 드세요. 그리고 수고비로 50코인이에요.");
			Q.Objectives = {
				Objective(EPixelObjective::Collect, TEXT("Moonleaf"), 3, TEXT("월광초 채집")),
				Objective(EPixelObjective::Kill, nullptr, 3, TEXT("슬라임 처치")),
			};
			Q.Reward.Coins = 50;
			Q.Reward.MaxHealth = 25.f;
		}
		{
			FPixelQuestDef& Q = Quests.AddDefaulted_GetRef();
			Q.Id = TEXT("Smithing");
			Q.Title = TEXT("대장간의 구리");
			Q.Giver = TEXT("Hilda");
			Q.TurnIn = TEXT("Hilda");
			Q.Requires = { TEXT("Training") };
			Q.OfferText = TEXT("화로는 식었고 견습생은 음유시인 한다고 도망갔어.\n"
				"서쪽 관문 너머 옛 광산에서 구리 광석 3개만 가져와.\n"
				"그러면 제대로 된 창을 하나 만들어 주지.");
			Q.ProgressText = TEXT("옛 광산 근처 바위에 구리가 초록빛으로 비쳐 보일 거야.");
			Q.CompleteText = TEXT("자, 균형 잡히고 날도 섰어. [Tab]으로 바꿔 들어.\n"
				"닿는 거리는 길지만 회복이 느려. 적은 창끝에 둬.");
			Q.Objectives = {
				Objective(EPixelObjective::Collect, TEXT("Copper Ore"), 3, TEXT("구리 광석 채굴")),
			};
			Q.Reward.UnlockWeapon = TEXT("Spear");
		}
		{
			FPixelQuestDef& Q = Quests.AddDefaulted_GetRef();
			Q.Id = TEXT("Companion");
			Q.Title = TEXT("마법의 불꽃");
			Q.Giver = TEXT("Ilsa");
			Q.TurnIn = TEXT("Ilsa");
			Q.Requires = { TEXT("FirstSteps") };
			Q.OfferText = TEXT("어깨 위의 그 작은 친구, 생각보다 재능이 있어요.\n"
				"내려놓고 [G] 고삐를 잡으세요 [T]. 놔두면 제멋대로 쏘거든요.\n"
				"그다음 허수아비에 볼트 [F]를 몇 번, 칼에 축복 [C]을 한 번!");
			Q.ProgressText = TEXT("내려놓기 [G], 고삐 [T], 볼트 [F], 축복 [C]. 돕고 싶어 하잖아요.");
			Q.CompleteText = TEXT("봤죠? [1] [2] [3]으로 따라오기, 대기, 집중 공격을 시키고\n"
				"[T]를 다시 누르면 알아서 마법을 써요. 잘 챙겨 주세요.");
			Q.Objectives = {
				Objective(EPixelObjective::Action, *PixelQuestActions::CompanionDeploy.ToString(), 1, TEXT("동료 내려놓기 [G]")),
				Objective(EPixelObjective::Action, *PixelQuestActions::ManualCast.ToString(), 1, TEXT("수동 시전으로 바꾸기 [T]")),
				Objective(EPixelObjective::Action, *PixelQuestActions::Bolt.ToString(), 3, TEXT("볼트 시전 [F]")),
				Objective(EPixelObjective::Action, *PixelQuestActions::Empower.ToString(), 1, TEXT("강화 시전 [C]")),
			};
			Q.Reward.Coins = 20;
		}
		{
			FPixelQuestDef& Q = Quests.AddDefaulted_GetRef();
			Q.Id = TEXT("SlimeKing");
			Q.Title = TEXT("공터의 왕");
			Q.Giver = TEXT("Maren");
			Q.TurnIn = TEXT("Maren");
			Q.Requires = { TEXT("Garden"), TEXT("Smithing"), TEXT("Companion") };
			Q.OfferText = TEXT("훈련장 동쪽 공터를 뭔가 큰 놈이 짓뭉개고 다닌다네.\n"
				"슬라임들이 새끼 오리처럼 그 뒤를 졸졸 따라다니고.\n"
				"그놈을 끝장내 주게. 브룩홀로우가 크게 빚지는 걸세.");
			Q.ProgressText = TEXT("공터는 훈련장을 지나 동쪽에 있네. 조심하게.");
			Q.CompleteText = TEXT("공터가 조용해졌군. 이제 자네도 이 마을 사람일세.\n"
				"이걸 받게. 그리고 오늘 밤은 푹 쉬게나.");
			Q.Objectives = {
				Objective(EPixelObjective::Kill, TEXT("SlimeKing"), 1, TEXT("슬라임 킹 처치")),
			};
			Q.Reward.Coins = 100;
			Q.Reward.MaxHealth = 25.f;
		}
		return Quests;
	}

	TArray<FPixelNpcDef> BuildNpcs()
	{
		return {
			{ TEXT("Maren"), TEXT("마렌 촌장"), TEXT("브룩홀로우는 조용한 마을일세. 계속 그랬으면 좋겠군.") },
			{ TEXT("Brann"), TEXT("브란 대장"), TEXT("발은 벌리고, 눈은 적에게. 절대 칼을 보지 말게.") },
			{ TEXT("Orlen"), TEXT("약초사 오를렌"), TEXT("월광초가 다시 잘 자라고 있어요. 고마워요, 여행자님.") },
			{ TEXT("Hilda"), TEXT("대장장이 힐다"), TEXT("날카로우면 내가 만든 거야. 무뎌졌으면 다시 가져와.") },
			{ TEXT("Ilsa"), TEXT("마법사 일사"), TEXT("마법은 대부분 인내심이에요. 그리고 약간의 고함이요.") },
		};
	}
}

const TArray<FPixelQuestDef>& PixelQuestCatalog::GetQuests()
{
	static const TArray<FPixelQuestDef> Quests = BuildQuests();
	return Quests;
}

const FPixelQuestDef* PixelQuestCatalog::FindQuest(FName Id)
{
	return GetQuests().FindByPredicate([Id](const FPixelQuestDef& Quest) { return Quest.Id == Id; });
}

const FPixelNpcDef* PixelQuestCatalog::FindNpc(FName Id)
{
	static const TArray<FPixelNpcDef> Npcs = BuildNpcs();
	return Npcs.FindByPredicate([Id](const FPixelNpcDef& Npc) { return Npc.Id == Id; });
}

FString PixelQuestCatalog::GetItemName(FName ItemId)
{
	static const TMap<FName, FString> Names = {
		{ TEXT("Moonleaf"), TEXT("월광초") },
		{ TEXT("Copper Ore"), TEXT("구리 광석") },
		{ TEXT("Coin"), TEXT("코인") },
		{ TEXT("Spear"), TEXT("창") },
		{ TEXT("Sword"), TEXT("검") },
	};
	const FString* Name = Names.Find(ItemId);
	return Name ? *Name : ItemId.ToString();
}
