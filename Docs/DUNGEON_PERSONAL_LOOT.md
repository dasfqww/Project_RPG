# 던전 개인별 추첨 보상

2026-11-01 backend 구현에 이어2026-11-02 사용자 승인으로 Docker를 기동하고
전용 PostgreSQL의 실제 seed 복원/응답 유실 재시도/가방·우편 지급을 확인했다.
UE 보상 정의·고정 payload·송신 필드와 실제 지급 수량 안내를 연결했고 정상 빌드·
집중 native8 및 첫 보스 일반/고난도 보상 에셋2개 작성·재로드·원본 추가를 완료했다.
기존 서비스 배포나 실제 첫 보스 던전 공개/서버 참조 연결을 완료한 것은 아니다.

## 규칙과 요청

기존 `POST /api/dungeon-sessions/{sessionId}/settle-rewards`의
`itemRewards[]`에 선택 필드 `personalLootWeight`를 추가한다.

- 생략/null: 기존처럼 모든 정산 수령인에게 확정 지급.
- 1~10000: 하나의 배타적 개인 추첨 풀에 들어가는 1만 분율 가중치.
- 양의 가중치 합은10000 이하. 남는 구간은 미당첨이며 최대 한 **항목**만 선택한다.
- 0/음수/10000 초과·합 초과는400. 추첨 항목에는 실제 `maxStackSize > 0`도 필요하다.
- 항목 수16·기존 정의/수량/귀속/성장 상태 검사는 유지한다. 클라이언트/관리자에게 새 정산
  권한을 주지 않으며 기존 배정 GameServer 토큰만 제출한다.

첫 구간의 기획은 광석4/증표2를 확정 항목으로 두고 아래 네 항목을 각각500으로 등록한다.
이때 장비20%·각5%·미당첨80%이며, 파티원끼리 주사위를 경쟁하지 않는다.

| DefinitionName (`RPGItemDefinition`, Version1) | 개인 가중치 |
|---|---:|
| `GameItem.Equipment.Progression.Target.EnduranceGreatSword` | 500 |
| `GameItem.Equipment.Progression.Target.CirculationGreatSword` | 500 |
| `GameItem.Equipment.Progression.Target.Chest` | 500 |
| `GameItem.Equipment.Progression.Target.Gloves` | 500 |

예를 들어 장비 한 항목은 아래와 같다. 네 항목을 함께 넣어야 위 분포가 된다.

```json
{
  "definitionType": "RPGItemDefinition",
  "definitionName": "GameItem.Equipment.Progression.Target.EnduranceGreatSword",
  "definitionVersion": 1,
  "quantity": 1,
  "bindState": "CharacterBound",
  "durabilityCurrent": 0,
  "durabilityMaximum": 0,
  "instanceTags": [],
  "statValues": [],
  "maxStackSize": 1,
  "equipmentProgression": { "growthBandId": "FirstSlice", "ruleVersion": 1, "level": 0 },
  "personalLootWeight": 500
}
```

장비 이름이나 콘텐츠 ID로 추첨을 자동 추론하지 않는다. 기존 정의는 가중치 필드가 없어
그대로 확정 보상이며, 이번 코드만으로 현재 보스 에셋이20% 드롭을 시작하는 것은 아니다.

## 결과 고정과 지급

새 정산을 확정하는 기존 세션 잠금/transaction 안에서 backend만32바이트 비밀값을 만든다.
추첨 항목의 storage-only `personalLootSeed`로 기존 `item_rewards` JSON에 함께 저장한다.
공개 요청에는 seed 인자가 없고 정산 응답/아이템 snapshot에도 내보내지 않는다. 미지원/잘못된
요청은 작업을 만들지 않으며 재확인 시 기존 작업을 먼저 반환하므로 seed를 다시 만들지 않는다.

Worker는 고정 seed를 키로 세션/캐릭터/보상 버전/v1 구분자를 HMAC-SHA256에 넣는다.
모듈로 편향은 rejection sampling으로 제거한다. 수령인별 결과는 독립이며 같은 수령인의
재처리/작업자 교체/DB 복원은 같은 항목 인덱스를 선택한다. 누락/불일치 seed가 있는 새 형식은
다시 추첨하거나 전부 주지 않고 실패시킨다. 공개 session GUID만으로 사전 결과를 알 수 없다.

기존 정산의 수령인 자격 캡처를 그대로 사용한다. 포기/유예 만료로 제외된 캐릭터를 추첨으로
되살리지 않으며 이후 작업자에서 수령인을 다시 계산하지 않는다. 확정 항목과 선택된 항목만
기존 결정적 Item ID/요청 ID로 만든다. 원래 항목 인덱스를 유지하고 수량·중첩 한도는 각
수령인 요청에 맞춰 전달한다. 가방 중첩/빈 칸 우선·초과분 우편·전체 파티 원자 Commit 및
성공 영수증 우선 복구는 기존 경계를 사용한다. 미당첨을 이유로 확정 재료를 빼지 않는다.

가중치/seed가 없는 옛 JSON과 fingerprint는 변경하지 않는다. 새 스키마/컬럼/DDL·기존
정산 보정은 없다. **다만 가중치 정산의 구형 Worker와 혼합 실행은 지원하지 않는다.**
구형 바이너리는 새 필드를 모른다. 배포 시 모든 Worker를 새 버전으로 전환한 뒤 추첨
콘텐츠 송신을 켜야 하며 rolling upgrade/운영 전환을 이번 작업의 성공으로 간주하지 않는다.

## 확인 범위와 후속

### 2026-11-02 UE 연결과 첫 보스 보상 에셋 완료

- `FRPGDungeonItemRewardEntry`/전송 구조체의 `PersonalLootWeight`는 기본0이다.
  0은 JSON에서 생략해 옛 확정 보상을 유지하고, 양수만 `personalLootWeight`로 쓴다.
  정의 builder·GameMode 정산·HTTP factory 모두 범위/합/양수 stack 한도를 검사한다.
  추첨/seed 생성은 UE가 하지 않으며 입장 때의 복사본에 가중치도 고정한다.
- 기존 GameMode 보상 갱신은 선택 버전 query로 실제 수령인의 아이템 영수증 수량을
  사용한다. 미당첨 후보를 합산하지 않으며 구형 응답의 추첨 수량을 추정하지 않는다.
  확정 전용 보상만 기존 fallback을 유지한다.
- 새 작성기 `Scripts/Editor/CreateProgressionBossRewardAssets.py`는 기본 읽기 전용,
  `-RPGCreateProgressionBossRewards`일 때 누락된 두 보상만 만들고, 기존/dirty/custom
  자산은 덮어쓰지 않는다. `RPG_PERSONAL_LOOT_ASSET_REPORT`는 절대 경로로 지정한다.
  `-RPGProgressionBossRewardsReload`는 저장 없이 재로드 확인한다.
- 원본에 추가한 경로는
  `Content/Blueprints/GameData/DungeonReward/Progression/DA_DungeonReward_FirstBossNormal.uasset`,
  같은 폴더의 `DA_DungeonReward_FirstBossHard.uasset`다. 두 정의 모두 `FirstSlice.v1`,
  재화0·광석4/증표2 확정·장비4종 각각500·CharacterBound·명시적 +0이다.
  의도한 ID는 metadata의 `Progression.FirstBoss`이며 기존 `Dungeon.RaidSwordMan` 보상을
  이 정의로 바꾸지 않았다.

정상 타깃419작업/종료0·입력1364개 파일집합/SHA 불변·기존 deprecated 경고5다.
새 PersonalLoot3 + 기존 EquipmentProgression4 + 변경 Snapshot.Codec1 모두 무경고
Success/UE0이며 기동 Error15와 검사 이후 Error0을 구분한다. 모듈 `06E2BA60…E9A6C892B`의
실제 로드를 관측했고 UE11 원본 hash도 일치했다. 별도 content 사본에서2개 작성/Valid,
새 프로세스 재로드0저장/동일 hash 후 원본 누락2개만 추가했다.

최초 자기 빌드는 지급 수량 수정까지 한 cohort에 넣으려고 중단했고 후속 정상 성공과
구분했다. 에셋 최초 검사는 작성/재로드에는 성공했지만 Python 소문자/PS 대문자 SHA의
대소문자 비교로 원본 설치 전에 멈췄다. runner 비교만 수정한 후 에셋 단계만 성공했고,
게임 재빌드/native8 반복은 하지 않았다. 마지막 요약기의 PS 로그 문자열 확장 속성은
순수 문자열로 복사해 작은 JSON으로 보존하며 기능 검사를 다시 실행하지 않았다.

이번 PG/UE 모든 사본·DLL/obj/UserDir/캐시325,445,311 bytes(310.369MiB)를 자동 폐기했다.
자기 임시/UE/PG 컨테이너·정리 예외0이며 원본 정상 빌드 출력과 새 에셋2는 유지한다.
폐기 사본의 hash는 당시 증거다. 전체 결과/경고·실패/선택 소스 hash는
`Saved/BuildChecks/PersonalLootIntegration-20261102/Summary.proof.json`에 있다.

본인 이번 연결 범위는 완료·예약 해제다. 실제 `Progression.FirstBoss` 맵/서버 정의의
Normal/Hard 참조 연결은 해당 콘텐츠 구현과 조율해 진행해야 하며 로비에 임의 공개하지
않았다. 실제 UE 던전→PG 정산/화면 안내·Steam/Shipping/cook·기존 서비스 배포는
이번 native/HTTP/PG 저장소 검사로 확인한 것이 아니다. 구형 Worker 혼합 금지도 유지한다.

### 2026-11-02 PostgreSQL 실행과 수량 안내 연결

`--personal-loot-postgres-only`는 전용 연결이 없으면 실패하며 skip하지 않는다.
최종 입력115개의 닫힌 사본/Release 빌드0에서 PG2그룹과 실제 backend HTTP가
통과했다. 첫 PG 실패는32자리 테스트 fingerprint가 PostgreSQL CHAR(64)에서
패딩되어 재요청과 달라진 것이며, 실제 API와 같은64자리 SHA256 fixture로 고쳤다.
제품의 fingerprint 저장/비교 규칙은 바꾸지 않았다. seed 복원·claim/requeue·실제
commit 응답 유실 후 동일 batch/중복0·개인 장비 +0·포기자 제외·부분 중첩/포화 우편을
확인했다. 기존 Memory 저장소의 완료 검사를 대기용으로 반복하지 않았다.

UE 후보 전체 합계가 안내 수량에 사용되던 경로를 실제 지급 영수증 수량으로 연결한다.
선택 `reward-snapshot?rewardVersion=...`의 `delivery` 계약과 legacy fallback 조건은
[실시간 갱신 계약](DUNGEON_REWARD_LIVE_REFRESH.md#개인-추첨의-실제-지급-수량-2026-11-02)을 따른다.
실제 HTTP에서 확정6개+선택 장비0/1개만 안내 수량에 들어가고 seed가 공개되지 않음을 확인했다.
Worker와 snapshot은 같은 결정적 아이템 명령 ID 함수를 사용하며 저장 형식은 불변이다.

전용 tmpfs 컨테이너3개를 각각 종료·폐기하고 입력/artifacts/obj 사본29,957,863 bytes를
자동 정리했다. 최초 fixture 실패·수정 후 성공·지급 수량 후속 성공은 별도 근거로 남겼다.
Docker Desktop은 사용자 요청으로 켠 상태를 유지하며 기존 컨테이너/DB는 수정·종료하지 않았다.
근거: `Saved/BuildChecks/PersonalLootIntegration-20261102/Postgres/`의 각 `Result.json`.

### 2026-11-01 당시 확인 범위 (아래 PG/UE 공백은 당시 이력)

최종 닫힌 입력117개 사본에서 두 Release 빌드 종료0/경고·오류0,
`--personal-loot-only`4그룹과 기존 RewardDelivery Memory25개가 통과했다.

- 정확히10000개 bucket을 전수 검사:80% 미당첨/네 장비 각5%, 확정 재료 유지.
- JSON 복원·수령인 분리·seed/잘못된 pool/구형 JSON 검사.
- 실제 backend 진입점의 Bearer HTTP: 플레이어/잘못된 가중치 거절·세션 불변,
  올바른 정산→Worker→snapshot, seed 비노출.
- 실제 Memory Worker: 포기한4번째 멤버 제외, claim/requeue의 seed 고정,
  실제 Commit 뒤 응답 유실→동일 batch 재시도/중복0·가방 포화 우편/부분 중첩.
  저장소 fixture는 장비 항목 하나가 반드시 선택되도록4×2500을 사용해 +0 보존 검사가
  미당첨으로 비어 통과하지 않게 했다. 이는 운영20% 수치를 바꾼 것이 아니다.

PostgreSQL 코드와 새 pool 복원/실제 Worker 검사는 작성·컴파일했지만 엔진 부재로
**실행하지 않았다**. 기존 DB를 대신 쓰거나 Docker/서비스를 임의로 시작하지 않았다.
다음은 허용된 전용 PG 실행과 UE 보상 정의/송신 연결이며 현재 타 담당의 HttpWebManager.h/
공유 빌드 범위를 확인·인계한 뒤 진행한다. 원본 콘텐츠/UE/공유 DLL·기존 서비스 배포는 미변경이다.

첫 Docker 연결 실패와 초기 정리 가드 실패를 별도 보존했고, 남은 자기 임시 사본은
정확한 소유 경계/프로세스 사용 해제 확인 뒤 복구 정리했다. 새 추첨 보상의 한도0에 의한
우편 강제를 막도록 보완한 최종 실행도 별도 구분한다. 전체 세 사본/artifacts/DLL/obj
41,209,366 bytes(39.30MiB) 폐기·최종 잔여/정리 예외0·컨테이너 생성0이다.
작은 명령/입력 hash·실패/최종 로그와 결과만
`Saved/BuildChecks/PersonalLootBackend-20261101/`에 보존한다. 폐기 모듈 hash는 당시 근거다.
