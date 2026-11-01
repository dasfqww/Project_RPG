# 첫 성장 구간의 영속 성장 API

갱신일: 2026-10-26

현재 구현은 [첫 성장 구간 v0.1](../Docs/PLAYER_PROGRESSION_FIRST_SLICE_V01.md)의 **경험치·SP·스킬 습득·스킬 투자·트라이포드 선택·8슬롯 저장 및 UE 서버 복원 연결**이다. Memory와 PostgreSQL, 인증된 HTTP 경로와 Pawn의 기록 공급자 소스를 연결했다. 실제 훈련·던전 콘텐츠, 고유 능력·트라이포드 효과 데이터, 장비 강화·제작·개인별 장비 추첨은 후속 연결 대상이다.

## 1. 원본과 생성

- 프로필 ID: `FirstSlice.GreatSword.v1`.
- 직업 ID: `RPGPlayerClassDefinition:GreatSwordWarrior`. 기존 `DA_Class_GreatSwordWarrior`의 안정 ID와 일치한다.
- 새 캐릭터 생성 시 0 경험치·레벨 1·0 SP·두 스킬과 처음 두 슬롯을 저장한다. 시작 무기와 성장 기록은 PostgreSQL에서 캐릭터 생성 트랜잭션에 함께 들어간다.
- 기존 캐릭터의 누락된 기록은 자동 생성하지 않는다. 조회 실패나 누락을 새 캐릭터로 취급하지 않는다. 기존 캐릭터의 명시적 이관 정책은 미구현이다.
- 캐릭터 레벨은 `경험치 / 100 + 1`, 상한 경험치는 900이다. 총 SP는 레벨 1~10에서 `0,3,6,9,12,14,15,16,17,18`이다.
- 스킬 투자 비용은 2~4레벨 각 1 SP, 5~7레벨 각 2 SP이며 최대 스킬 레벨은 7이다. 사용 SP는 저장된 스킬 레벨에서 서버가 다시 계산한다.

초안의 `RPGPlayerClassDefinition:GreatSword`는 실제 등록된 에셋 ID와 달랐다. `FirstSlice.GreatSword.v1` 프로필의 이 정확한 초안 ID만 `GreatSwordWarrior`로 변환하고 revision을 한 번 올린다. 경험치·SP·선택·슬롯을 보존하며 이미 변환된 기록에는 다시 적용하지 않는다. 미지의 클래스나 다른 프로필은 변환하지 않고 거절한다. 조회의 상태·중복 지급 기록은 같은 잠금/트랜잭션 아래 처리하며 과거 명령 영수증은 최초 결과를 그대로 유지한다. 따라서 과거 영수증의 클래스 ID와 revision이 이전 값일 수 있고 최신 상태 위에 덮어쓰지 않아야 한다.

## 2. 클리어에서 경험치가 확정되는 조건

경험치 지급 요청 API는 제공하지 않는다. 기존 `dungeon_reward_settlements`의 `Completed` 기록 중 수령 캐릭터가 `character_ids`에 포함되고 `completed_at`이 있는 기록만 사용한다. `Pending`·`Processing`·`Failed`와 보상 대상에서 제외된 멤버는 성장 지급 근거가 아니다.

보상 버전이 정확히 `FirstSlice.v1`이고 다음 콘텐츠 ID·난이도 조합인 경우, 1회에 경험치 100과 해당 진행 플래그를 적용한다. 동일한 캐릭터·세션 조합은 `character_progression_awards`에서 한 번만 확정한다. 재접속·조회·저장 재시도로 경험치를 다시 주지 않는다.

| 던전 ID | 난이도 | 필요 플래그 | 완료 플래그 | 신규 해금 |
|---|---|---:|---:|---|
| `Progression.Training` | `Normal` | 0 | 1 | 회전 베기·고유 능력 |
| `Progression.Intro1` | `Normal` | 1 | 2 | 도약 강타 |
| `Progression.Intro2` | `Normal` | 3 | 4 | 두 번째 입문 진행 |
| `Progression.Intro3` | `Normal` | 7 | 8 | 일반 보스 진입 |
| `Progression.FirstBoss` | `Normal` | 15 | 16 | 고난도 진입 |
| `Progression.FirstBoss` | `Hard` | 31 | 32 | 고난도 완료 기록 |

필요 플래그는 비트 합계다. 이미 연 진행을 다시 클리어해도 새 세션이면 상한까지 경험치를 얻는다. 레벨 상한 뒤에도 아직 받지 않은 고난도 완료 플래그는 저장한다.

성장 조회·구성 저장·첫 구간 입장 자격 조회에서 미반영 완료 정산을 확인한다. PostgreSQL에서는 성장 상태와 중복 지급 방지 기록을 같은 트랜잭션으로 저장한다. 기존 재화·아이템 정산의 완료 뒤 지연 반영하는 방식이며, 해당 정산과 성장 갱신을 하나의 DB 트랜잭션으로 합친 것은 아니다. 성장 반영 전에 프로세스가 종료돼도 다음 조회에서 완료 정산을 다시 확인할 수 있다.

필요 진행이 아직 확인되지 않은 정산은 지급 완료로 소모하지 않는다. 앞선 정산 반영 뒤 다시 조회할 수 있다. 첫 구간의 세션 생성·참가 API에도 필요 진행 검사를 연결했다. 기존 콘텐츠 ID는 이 프로필의 진행 제한을 추가로 받지 않는다.

**이 ID들은 아직 운영 콘텐츠 목록과 UE 맵/보상 정의에 등록하지 않았다.** 운영 적용 시 백엔드 콘텐츠 목록, 서버 던전 정의와 보상 버전을 함께 연결한다. 기존 보스 보상에 버전만 임의로 바꿔 성장 경로 전체가 완성된 것으로 취급하지 않는다.

## 3. 성장 조회

`GET /api/characters/{characterId}/progression`

자기 캐릭터의 플레이어, 해당 활성 세션에 배정된 서버, 관리자가 조회할 수 있다. 다른 플레이어는 403이다. 저장 기록이 없으면 404 `progression_not_initialized`다. 종료된 서버 자격증명의 기존 제한을 넓히지는 않는다.

응답에는 다음 값이 있다.

- `characterId`, `profileId`, `classId`.
- 문자열 `revision`과 정수 `experience`, `level`, `totalSP`, `usedSP`.
- `milestones`, `identityUnlocked`.
- 습득한 `skills`: `skillTag`, `level`, 길이 2의 `tripods`.
- 길이 8의 `skillSlots`. 빈 위치는 빈 문자열이다.

서버가 저장한 습득 목록만 반환하며 새로 습득한 스킬을 빈 슬롯에 자동으로 넣지 않는다. 시작 스킬은 `Player.Ability.Skill.AssultBlade`와 `Player.Ability.Skill.Charge`, 훈련 해금은 `Player.Ability.Skill.WhirlWind`, 첫 입문 해금은 `Player.Ability.Skill.JumpSmash`다. 이 태그는 현재 UE 태그 정의와 일치한다.

## 4. 스킬·트라이포드·편성 저장

`PUT /api/characters/{characterId}/skill-configuration`

자기 캐릭터의 플레이어만 호출할 수 있다. 서버·관리자·다른 플레이어의 대리 저장을 허용하지 않는다. 현재는 유효한 던전 리스가 없는 상태에서만 저장한다. 따라서 Waiting 파티에 참가한 상태도 잠기며, 대기 로비에서 구성 변경까지 허용하는 확장은 별도다.

```json
{
  "requestId": "b79c6f36-6681-4eb8-b370-2164116afc64",
  "expectedRevision": "1",
  "skills": [
    { "skillTag": "Player.Ability.Skill.AssultBlade", "level": 1, "tripods": ["", ""] },
    { "skillTag": "Player.Ability.Skill.Charge", "level": 1, "tripods": ["", ""] }
  ],
  "skillSlots": ["Player.Ability.Skill.AssultBlade", "Player.Ability.Skill.Charge", "", "", "", "", "", ""]
}
```

전체 습득 스킬을 중복 없이 한 번씩 보내고 8개 슬롯을 함께 보낸다. 사용 SP가 총 SP를 초과하거나, 미습득 스킬·중복 슬롯·범위 밖 레벨·해금 전 트라이포드를 포함하면 거절한다. 재분배 자체에는 비용이 없다. 레벨을 낮춰 트라이포드 자격을 잃으면 해당 선택을 빈 문자열로 지워서 제출해야 한다.

트라이포드는 배열 순서에 따른 옵션 인덱스를 영속 저장하지 않고 다음 안정적인 선택 ID를 저장한다. 두 배열 위치는 스킬 4레벨과 7레벨의 티어다. 빈 문자열은 미선택이다.

| 스킬 | 4레벨 선택 ID | 7레벨 선택 ID |
|---|---|---|
| 돌진 | `LongReach`, `QuickApproach` | `ExtraReach`, `ExtraRecovery` |
| 차지 | `FullCharge`, `QuickCharge` | `Stagger`, `PartBreak` |
| 회전 | `Mobile`, `Stationary` | `WideArea`, `FinalStagger` |
| 도약 | `WideArea`, `PartBreak` | `ExtraArea`, `Stagger` |

UE에는 옵션의 `PersistenceId`를 해금 레벨 4·7과 함께 찾아 실제 옵션 인덱스로 변환하는 연결을 추가했다. 2026-10-26 차지의 4레벨 `FullCharge`/`QuickCharge`를 기존 옵션 뒤의 인덱스 1/2에 실제 작성·저장했다. 나머지 표의 ID·효과는 아직 콘텐츠 작성이 남아 있다. 저장 API 통과가 해당 효과의 GAS 실행 성공을 뜻하지 않는다.

같은 Request ID·같은 내용은 최초 결과를 돌려주고, 같은 ID에 다른 내용을 보내면 409 `idempotency_conflict`다. 오래된 revision은 409 `stale_progression_revision`, 던전 리스 중 변경은 409 `active_dungeon_configuration_locked`다. 잘못된 revision 문자열은 400이다. 거절된 구성은 저장하지 않는다.

성장 획득과 구성 저장은 같은 캐릭터의 잠금과 revision을 사용한다. 성장 반영으로 revision이 달라졌으면 새 상태를 읽어 다시 저장한다. 과거 요청의 재전송 응답은 현재보다 오래된 revision일 수 있다. 소비자는 이를 최신 상태 위에 덮어쓰지 않아야 한다.

## 5. 저장 및 검증 범위

- PostgreSQL: `character_progression`, `character_progression_awards`, `character_progression_commands`를 스키마에 추가했다. 새 캐릭터의 생성과 초기 성장 기록은 같은 트랜잭션이다. 기존 행에 자동 0 경험치를 채우지 않는다.
- Memory: 같은 게임 저장소의 캐릭터·던전 잠금 아래 성장 상태와 지급/명령 기록을 관리한다. 외부에 반환하는 배열은 복사해 호출자가 저장 원본을 바꾸지 못한다.
- 종료 정산·지급 기록은 재시도와 복원 근거다. 보존 정책 없이 이 기록을 정리하거나 누락을 초기값으로 덮지 않는다.
- 검증: Release 빌드 경고·오류 0. 기존 StarterEquipment 검사에 성장 사례를 추가해 정산 전 미지급, 중복 방지, 해금 순서, SP·트라이포드·편성 거절, 소유권, 실패 무보상, 상한, 과거 요청 재전송과 동시 revision 충돌을 확인했다.
- 실제 로컬 HTTP/Memory/개발 인증으로 생성·401/403·조회·저장·재전송·충돌·SP 초과·잘못된 revision·거절 후 상태 보존의 14개 검사를 통과했다. 결과는 `Saved/Build/CharacterProgression-1792922547201/http-checks.json`이다.
- PostgreSQL 코드와 스키마는 추가했지만 실제 DB 적용·재시작 복원·동시 SQL 실행은 아직 검증하지 않았다. Steam 운영 인증·UE에서의 실제 조회/복원·GAS·자연 전투 검증도 미실행이다. UE 소스와 최소 컴파일 범위는 6절을 따른다.

## 6. UE 서버 복원 연결 — 2026-10-25

`ARPGPlayer`의 기본 `URPGCharacterProgressionComponent`가 공통 시작 Ability 준비와 인증된 `ARPGPlayerState`를 기다린다. `PossessedBy` 안에서는 Controller의 Pawn 연결이 아직 끝나지 않을 수 있으므로 엔진의 Restart 알림에서도 준비를 확인한다. 별도의 틱·클라이언트 복원 RPC·사용자 입력 기록은 없다. `UHttpWebManager::LoadConfiguredCharacterProgression`은 설정된 게임 서버 자격증명으로 위 GET을 요청하고 플레이어 토큰을 대신 사용하지 않는다.

복원 순서는 다음과 같다.

1. 현재 Pawn·Controller·PlayerState·World와 캐릭터·Steam·세션·접속 세대를 고정한다.
2. HTTP 응답의 캐릭터 GUID, 숫자·revision·습득 스킬·8슬롯을 검증한다. 401·403·404·전송 실패·잘못된 기록을 기본 성장이나 개발 프로필로 대체하지 않는다.
3. 저장된 클래스의 Primary Asset ID를 AssetManager에서 해석해 비동기로 로드한다. 클라이언트가 보낸 에셋 경로를 사용하지 않는다.
4. 클래스 정의와 저장 스킬의 소속·레벨·트라이포드 ID·해금 조건·슬롯을 대조하고 기존 SP 비용 함수로 사용량을 다시 계산한다. 이름·배열 순서로 선택을 추측하지 않는다.
5. 기존 `URPGPlayerSkillComponent`의 서버 복원 → `URPGPlayerClassComponent`의 소유 Ability 부여 → `UQuickSlotComponent`의 1회 슬롯 복원을 사용한다. 빈 위치는 그대로 유지한다.
6. 완료한 기록의 XP·레벨·진행·backend revision과 공급자 상태/오류를 소유자에게만 복제한다. SP·습득·편성의 원본을 공급자 컴포넌트에 별도로 만들지 않는다.

HTTP 응답과 클래스 로드가 끝날 때마다 고정한 컨텍스트를 다시 확인한다. 늦은 완료는 새로운 Pawn·캐릭터에 반영하지 않는다. 부여·슬롯 단계가 실패하면 같은 복원 세대에 속한 성장과 클래스 핸들만 무효화한다. 다른 세대나 파티원의 상태를 회수하지 않는다. 같은 캐릭터·세션의 확인된 재접속은 이미 적용한 그랜트·자원·쿨타임을 유지하고 다시 부여하지 않는다. 이미 권한 회수된 Actor를 이 경로로 살리지 않는다.

backend 복원은 `identityUnlocked`를 GAS 부여 계획에 반영한다. 잠긴 고유 능력은 클래스에 Ability가 지정돼 있어도 부여하지 않는다. 반대로 저장상 해금된 고유 능력에 실제 Ability가 없으면 콘텐츠 누락으로 실패한다.

복원된 전투 Pawn은 저장 구성을 읽기 전용으로 사용한다. 기존 SP 변경·트라이포드·스킬 슬롯 RPC와 임시 `AddTotalSP`로 저장 상태와 다르게 바꿀 수 없다. 초기 복원 자체는 정확한 복원 세대와 1회 초기화 경계에서 허용한다. 아이템 슬롯과 일반 인벤토리 동작의 정책을 일괄 잠그지는 않는다. 로비에서 PUT을 호출하는 UE 편집·저장 경로는 7절을 따른다.

개발 프로필은 인증된 Pawn에 새로 적용하지 않는다. 이미 적용한 비영속/개발 기록이 남은 Pawn에 뒤늦게 인증을 붙여 운영 기록으로 승격시키지도 않는다. 이런 경우 새 Pawn에서 복원해야 한다. 초기화 실패 시에도 기존 기본값으로 재부여하거나 현재 Pawn의 쿨타임을 초기화하는 재시도를 하지 않는다.

콘텐츠 작성 시 `FRPGSkillTripodOption.PersistenceId`에 4절의 ID를 명시한다. 같은 티어 안의 중복 ID는 데이터 검증에서 거절한다. 저장 선택이 있는 티어의 해금 레벨·ID가 없거나 모호하면 복원을 거절한다. 미선택인 이전 에셋은 ID가 비어 있어도 기존 동작을 유지한다.

최소 컴파일은 `Project_RPGEditor Win64 Development -NoLink -ForceHeaderGeneration`으로 통과했다. [전체 소스 컴파일 로그](../Saved/BuildLogs/CharacterProgressionUE-1792934025991/Editor-NoLink-Final.log)와 Restart 알림 처리까지 반영한 [최종 UHT·증분 컴파일 로그](../Saved/BuildLogs/CharacterProgressionUE-1792934025991/Editor-NoLink-Handoff.log)의 결과가 `Succeeded`다. DLL 링크·교체·에디터 실행이나 UE 테스트의 통과를 뜻하지 않는다. backend의 클래스 ID 변환 추가 후에도 [Release 빌드](../Saved/BuildLogs/CharacterProgressionUE-1792934025991/Backend-Build.log)와 [기존 저장소 검사](../Saved/BuildLogs/CharacterProgressionUE-1792934025991/Backend-Checks.log)가 통과했다. 5절의 HTTP 14개 결과는 이전 backend 단계의 기록이며 이번 바이너리로 다시 실행한 결과가 아니다.

2026-10-26 [대검 클래스 바인딩과 차지 첫 선택](../Docs/SKILL_AND_TRIPOD_MODULAR_ARCHITECTURE.md#2026-10-26-투지-해방-바인딩과-차지-4레벨-선택-저장)을 실제 에셋에 저장해 클래스의 고유 능력 미설정과 차지 4레벨 두 ID의 누락은 해소했다. 나머지 선택 ID·효과는 아직 미작성이다. 실제 HTTP/Pawn/GAS/복제·재접속·PostgreSQL 변환/복원 검증은 수행하지 않았으므로 이 저장을 운영 기록의 실제 플레이 성공으로 세지 않는다.

주요 소스는 [복원 공급자](../Source/Project_RPG/Private/Progression/RPGCharacterProgressionComponent.cpp), [기록 해석·변환](../Source/Project_RPG/Private/Progression/RPGCharacterProgressionSnapshot.cpp), [서버 HTTP 요청](../Source/Project_RPG/Private/Manager/HttpWebManagerProgression.cpp)다. 로비 구성 저장은 7절에 후속 연결했으며 실제 대검 고유 능력·트라이포드 ID/효과 작성은 남아 있다. 전체 첫 성장 구간 구현 완료와 구분한다.

### 6.1 저장 구성의 실제 서버 복원과 차지 시전 — 2026-10-27

실행 중 확인한 두 차단을 수정했다. `RPGClassSkillEligibility`의 무기 조건은 영속 장비가
있으면 기존 `URPGAuthoritativeEquipmentComponent`의 현재 장비 스냅샷 종류와 실제 Actor의
일치를 검사한다. 공용 표현 Actor의 기본 `WeaponType=Count`를 저장 아이템의 종류로
사용하지 않는다. 장비 없음·수납·Actor 로딩 중에는 기존 시작 무기로 우회하지 않으며,
영속 장비가 없는 기존 Actor 경로는 유지한다. 직업별 분기나 새 장비 상태는 추가하지 않았다.

차지 Ability 에셋에는 일반 스킬인데도 이전 `Player.Ability.IdentitySkill` 분류가 남아 있어
상위 Ability의 분노 상태 조건에 막혔다. `ConfigureGreatSwordSkillAbilityTags.py`가 클래스의
4개 바인딩을 검사하고 잘못된 차지 하나만 `Player.Ability.Skill.Charge`로 교정했다.
다른 3개 파일은 바이트 단위로 동일하며, 관련 없는 태그·실행/인스턴싱 정책·발동 조건·
취소/차단 태그·Definition 연결을 보존했다. 별도 원본 백업과 저장 후 dirty=false,
새 commandlet의 4개 재조회·추가 변경 0건을 확인했다. 근거는
`Saved/ContentChecks/GreatSwordSkillTags/Applied.json`, `ReadOnly.json`이다.

집중 실행은 다음 명령을 사용한다.

```powershell
.\Scripts\Tests\Run-CharacterProgressionRestoreE2ETest.ps1
```

격리한 개발 인증/Memory에서 실제 훈련 정산·성장 GET으로 XP 100·레벨 2·SP 3을 얻고,
플레이어 PUT으로 차지 4레벨/FullCharge와 `[Charge, Assault, 빈칸×5, Whirlwind]`를 revision 3에
저장했다. 새 게임 서버와 클라이언트는 실제 티켓 소비·입장 확인·Iris를 거쳐 작성된
`bossmap`의 `BP_Player`를 사용한다. 테스트용 성장 기록 주입이나 개발 프로필 대체는 없다.

- 서버와 소유 클라이언트의 직업·성장·트라이포드·8슬롯, 습득 3개·일반 스킬 부여 3개·고유 능력 부여 1개가 일치했다. 미습득 도약은 부여되지 않았다.
- 기존 퀵슬롯 Press/Release로 서버의 실제 차지 시전을 관측했다. 고정 RuntimeSpec은 저장 레벨 4·FullCharge·총 충전 시간 1.2초였다. 시전 중 재알림/클래스 갱신 뒤에도 같은 활성 핸들을 유지하고 정상 종료했다.
- 서버의 추가 SP·편성 변경 시도는 읽기 전용 저장 구성을 바꾸지 않았다. 실제 장비 종류 GreatSword(3)와 표현 Actor의 Count(7)를 관측했고, 수납 상태의 발동 거절도 확인했다.
- 완료 뒤 성장 저장본 전체와 같은 계정의 다른 캐릭터 저장본이 각각 이전 값과 동일했다. 서버/클라이언트/러너 종료 0, 기동·실행 Error 0과 선택 입력 32개·DLL 전후 일치를 확인했다.

근거는 `Saved/Tests/CharacterProgressionRestore/6fd05d5f127f4e6595a84f2e6f39d4fb/Verification.proof.json`
및 서버/클라이언트 로그다. 모듈 SHA-256은
`DF9DEE21A76DE15D54C4A89FA37142DD544281A7455C8022AC89CE7BDE043AC8`이다.
최종 정상 Editor 빌드는 자체 실행의 C++ 컴파일·DLL 링크·타깃 기록 4개 작업/종료 0이며
`Saved/BuildChecks/ProgressionRestore-20261027/Editor-AimProbe.log`에 남겼다. 격리 backend Release는
경고/오류 0이다. 앞선 DLL 잠금 중 별도 실행 폴더의 링크·실패 실행과 최종 정상 프로젝트의
통과를 구분한다. 초기 조회·복원 성공만 있었던 실패 실행을 시전 성공으로 합산하지 않는다.

검사 종료 후 다른 작업이 공유 DLL과 `DA_Skill_AssultBlade` Definition을 갱신한 것은
보존했다. 위 통과는 기록한 모듈/입력으로 봉인한 해당 실행의 근거이며 후속 버전의
재검증을 뜻하지 않는다. 종료 후 대조는 `Saved/BuildChecks/ProgressionRestore-20261027/Verification.json`에 남겼다.

기본 조준의 장애물 거절은 유지했다. 집중 검사는 서버에서 맵의 빈 방향을 읽어 클라이언트의
일반 ControlRotation을 준비한 뒤 기존 입력/타기팅/서버 장애물 검증을 사용한다.
합성 TargetData·권한 우회·수동 피해·보스 강제 처치는 없다. 이는 nullRHI의 입력 경계 주입이며
실제 키보드/GPU·적중/피해·비용/쿨타임 수치·자연 전투 승리·Steam·PostgreSQL·프로세스 재시작/
재접속의 검증이 아니다. 다음은 복원한 구성의 실제 적중·피해·자원/쿨타임과 투지 운용 확인이다.

### 6.2 저장한 두 차지 구성의 적중·쿨타임·투지 순환 — 2026-10-27

아래 두 구성/1초의 실행은 수치 변경 전 근거다. 후속으로 [첫 전투 기준과 실제 12초 차지 검증](../Docs/SKILL_AND_TRIPOD_MODULAR_ARCHITECTURE.md#2026-10-27-대검-첫-전투-쿨타임과-돌진-회복-선택)을 마쳤다. 현재 러너는 작성된 12초를 검사한다.

가장 최근 후속은 [기여 수신기와 실제 누적 상태 복제](../Docs/SKILL_AND_TRIPOD_MODULAR_ARCHITECTURE.md#2026-10-27-차지와-도약의-기여-선택-및-공통-수신기)다. 캐릭터 저장 계약은 그대로이며 `-VerifyCombat`은 기존 차지 흐름과 함께 기본 기여 20→100, 취소/제외 대상의 기여 0과 소유 클라이언트 수신을 확인한다. 보스의 그로기/파괴 반응 완료를 뜻하지 않는다.

실제 적중 연결에서 투지 경로의 두 차단을 수정했다.

- `UPawnCombatComponent::GetCharacterCurrentEquippedWeaponType()`가 기존 영속 장비 스냅샷과 실제 현재 Actor의 일치로 종류를 읽는다. 일반 스킬과 대검 고유 능력이 이 조회를 함께 사용한다. 이전 고유 능력은 공용 Actor의 `Count`를 읽어 투지 획득/해방을 거절했다. 장비 상태를 새로 복제하거나 직업별 분기를 공통 컴포넌트에 추가하지 않았다.
- GAS는 `ServerOnly` 능력도 소유 클라이언트에서 `CanActivateAbility`를 확인한 뒤 요청한다. 투지 능력의 생존/자격 조회가 서버 권한을 필수로 요구해 요청을 막고 있었으므로, 복제된 상태로 양쪽에서 조회할 수 있게 했다. 실제 실행·게이지 변경·강화 기회 소비에는 명시적인 서버 권한 검사를 유지했다. 게이지나 강화 값을 받는 클라이언트 RPC는 추가하지 않았다.

집중 검사는 기존 입장/복원 러너의 `-VerifyCombat` 모드와 `-ChargeChoice FullCharge|QuickCharge`로
실행한다. 실제 정산으로 얻은 SP 3을 플레이어 PUT으로 저장하고, 새 서버/클라이언트가
실제 티켓·입장 확인·Iris를 거쳐 같은 레벨 4/8슬롯 기록을 복원한다. 마나 비용이나 쿨타임,
트라이포드 수치를 검사 중에 덮어쓰지 않는다. 이 실행 당시 차지 작성값은 마나 비용 0·GAS Cost GE 없음·
기본 쿨타임 1초이며, 양수 마나 비용의 충분/부족 판정이나 밸런스 완료를 뜻하지 않는다.

검증 조건에서의 서버 HP 감소와 소유 클라이언트 수신 결과는 다음과 같다. 정수 피해
변환 전 고정 배율이 해방 시 1.25이므로 정수 값의 비율은 정확히 1.25가 아닐 수 있다.

| 저장 선택 | 일반 적중 1회 | 해방 후 적중 | 전체 6회 HP 감소 | 투지 순환 |
|---|---:|---:|---:|---|
| FullCharge | 571 | 714 | 3,569 | 0→20→40→60→80→100, 소비 100→0, 다음 적중 후 20 |
| QuickCharge | 464 | 580 | 2,900 | 동일 |

각 실행은 조기 해제 1회에서 실제 피해 GE/몽타주 AOE 이벤트/투지 획득 0을 확인했다.
취소 후 남은 쿨타임 중 기존 퀵슬롯 입력의 재시전 거절, 만료 후 다음 시전,
서버의 Definition별 쿨타임 GE 1개와 소유 클라이언트의 실제 서버 GE 수신을 확인했다.
일반 적중은 승인된 피해 GE/작성된 몽타주 이벤트 각 1회와 투지 20을 생성했다.
아군·뒤쪽·범위 밖 표적 3개는 모든 시전에서 피해가 없었다. 클래스/복원 재알림은 활성
핸들과 진행 중 쿨타임을 교체/초기화하지 않았다. 저장 성장 전체와 같은 계정의 다른
캐릭터 기록도 완료 뒤 각각 이전과 동일했다.

두 실행 모두 서버/클라이언트/러너 종료 0, 기동·실행 Error 0,
선택 입력 40개·DLL 전후 일치다. 모듈 SHA-256은
`05EBE9A2408C0801C0A1826B92755F88432951FEFBF3E5AFDFDAF9ED04880024`이다.
근거는 다음 각 폴더의 `Verification.proof.json`, `Combat.proof.json` 및 닫힌 실행 로그다.

- FullCharge: `Saved/Tests/CharacterProgressionRestore/ddafa52b718e41a9918b65a789674899/`
- QuickCharge: `Saved/Tests/CharacterProgressionRestore/ba000c1cc4824235bb19ba6c28802a7a/`

타입 조회 회귀는 `ProjectRPG.Item.EquipmentCombat.`의 기존 3개 자동화에 정상/다른 종류/
미지 종류·Actor 대기/수납/파괴 사례를 보강해 3개 Success·종료 0을 확인했다.
native 실행의 테스트 시작 전 기존 `Condition failed` Error 15줄과 테스트 시작 후 0줄은
위 실제 서버/클라이언트의 Error 0과 구분한다. 로그는
`Saved/BuildChecks/ProgressionCombat-20261027/Native-Equipment.log`다.

최종 C++ 컴파일 후 공유 DLL 잠금 동안 동일 링크 입력으로 별도 프로젝트 실행 폴더를
만들어 위 두 실행을 완료했다. 다른 검사 프로세스는 종료하지 않았다. 잠금 해소 뒤
정상 Editor 타깃의 DLL 링크·타깃 기록 2개 작업/종료 0도 확인했다
(`Saved/BuildChecks/ProgressionCombat-20261027/Editor-Final.log`). 별도 검증 모듈 해시와
정상 출력 DLL 해시는 구분한다. 준비 과정의 공유 테스트 컴파일 오류에서는 UE 5.8
`GetPawn()` 반환형을 명시적인 `APawn*`로 받도록 한 곳만 보완했다. 해당 소비 복구
시나리오 전체를 검증한 것으로 세지 않는다.

이 검사는 작성된 `bossmap`/`BP_Player`에서 입장·복원하지만, 적중 구간은 맵 위쪽의
정지 자세와 기존 복제 표적 4개를 사용하는 fixture다. 표적 체력/방어 초기화와 시전자
치명 확률 0만 준비하고, 실제 몽타주·입력·서버 타기팅/피해 GE를 관측한다.
피해 함수 호출·합성 피해 이벤트·게이지 주입·보스 강제 처치는 없다. 느린 엔진 초기
로딩 동안에는 기존 네트워크 검사와 같은 할당 서버 자격의 startup heartbeat를 보냈다.
운영 lease/입장 규칙을 바꾸지 않았으며 운영 서버 시작 준비의 검증으로 확장하지 않는다.
자연 보스전·이동/피격 중 운용·GPU/실물 입력·Steam·PostgreSQL·재시작/재접속은 별도다.

다음은 현재 마나 0/쿨타임 1초를 출시 수치로 취급하지 않고, 공통 비용·쿨타임 기준과
남은 트라이포드를 정리한 뒤 자연 전투와 실제 HUD/입력에서 두 구성의 체감을 확인하는 것이다.

## 7. UE 로비의 성장 구성 편집·저장 — 2026-10-25

기본 온라인 로비의 캐릭터와 던전 섹션 사이에 `URPGCharacterConfigurationWidget`을 연결했다. 소유 목록의 캐릭터를 선택하고 **저장본 다시 읽기**를 눌러야 해당 캐릭터의 기록을 조회한다. 누락된 기록을 자동 생성하거나 기본 SP·개발 프로필로 대체하지 않는다.

- 현재 화면은 명시적인 `FirstSlice.GreatSword.v1`과 `GreatSwordWarrior`를 지원한다. 스킬·선택 이름, 최대 레벨 7과 SP 미리보기는 이 버전의 UI 계약이다. 미지의 프로필/선택을 임의 변환해 편집하지 않으며 서버 검증이 최종 권한이다.
- 습득한 스킬만 표시한다. 총/사용 SP, 스킬 1~7레벨과 4·7레벨 트라이포드를 편집하고 SP 초과를 미리 거절한다. 레벨을 낮춰 자격을 잃으면 해당 선택을 편집본에서 해제한다. 다시 올려도 선택을 자동 복원하지 않는다.
- 전투 슬롯은 8개이며 빈칸을 유지한다. 이미 다른 슬롯의 스킬을 선택하면 빈칸으로 이동하거나 두 슬롯을 맞바꾼다. UI가 스킬을 습득시키거나 중복 슬롯을 만들지 않는다.
- **변경 취소**는 확인한 저장본으로 로컬 편집본만 되돌린다. **저장본 다시 읽기**는 서버를 조회해 편집본을 교체한다. 편집만으로 Pawn·GAS·쿨타임·자원을 갱신하지 않는다.
- 활성 던전의 로비에서도 조회는 가능하다. 새 편집·저장은 미완료 던전, Steam 파티, 미확정 출발·포기와 요청/이동 중에 잠근다. 리스가 UI보다 먼저 바뀌어도 백엔드의 캐릭터 잠금·리스 검사가 새 PUT을 거절한다.

`UHttpWebManager::LoadPlayerCharacterProgression` / `SavePlayerCharacterConfiguration`은 플레이어 인증만 사용하고 서버·관리자 자격증명으로 대체하지 않는다. 한 번에 조회 또는 저장 하나만 처리하고 게임 스레드에서 완료한다. 원래 요청 토큰과 계정이 일치할 때만 응답과 401 무효화를 처리해 늦은 응답이 새 로그인 토큰을 지우지 못하게 한다.

저장은 확인한 backend revision과 새 Request ID, 전체 습득·선택·8슬롯을 함께 보낸다. 요청 중에는 편집·주요 로비 전환을 잠근다. 전송 실패·타임아웃·5xx·해석할 수 없는 성공 응답은 커밋 미확정으로 취급해 원래 ID·revision·내용을 보존한다. **저장 결과 재확인**은 이 동일 명령을 재전송한다. 401 뒤에는 같은 계정으로 재인증한 뒤 재확인할 수 있다. 미확정 상태에서는 캐릭터 전환·새 던전·입장·로그아웃도 잠근다.

명확한 revision·리스·구성 충돌은 자동 병합·덮어쓰기 없이 새 조회를 요구한다. 이미 확인한 revision보다 오래된 결과, 지원하지 않는 프로필/선택, 저장 전 revision 이하의 성공 결과를 편집본에 덮어쓰지 않는다. 재전송 영수증이 서버 최신 상태보다 오래됐을 수 있으므로 새 조회로 현재 상태를 확인할 수 있다.

콜백은 요청 세대와 화면의 World·Controller·캐릭터·계정을 대조한 뒤 적용한다. 화면 해제/선택 변경으로 늦은 UI 콜백을 무효화하되 서버에 도착한 PUT을 되돌렸다고 취급하지 않는다. 편집본과 미확정 명령은 해당 위젯 메모리에만 있다. 앱 재시작 뒤에는 저장본을 다시 읽으며 자동 재전송·디스크 저널은 이번 범위에 추가하지 않았다. 무료 구성의 전체 상태 저장과 서버의 revision·멱등 경계는 유지한다.

커스텀 UI는 `RPGOnlineLobbySubsystem.GetLobbyWidget` → `GetCharacterConfigurationWidget`을 통해 `LoadSavedConfiguration`, `SetSkillLevel`, `SetTripodChoice`(티어 0/1), `SetCombatSlot`(슬롯 0~7), `SaveConfiguration`, `ResetDraft`를 사용한다. true는 로컬 편집 또는 요청 시작이며 저장 완료 보증이 아니다. 버튼과 BP 호출 모두 현재 로비·소유 선택·인증·요청 상태를 다시 검사한다.

소스는 [로비 편집 화면](../Source/Project_RPG/Private/UI/Lobby/RPGCharacterConfigurationWidget.cpp), [플레이어 HTTP 요청](../Source/Project_RPG/Private/Manager/HttpWebManagerProgression.cpp)이다. UHT와 관련 CPP 7개 `-FileList -NoLink`의 [최소 컴파일](../Saved/BuildChecks/LobbyCharacterConfiguration-20261025/Compile-Fixed.log)이 종료 코드 0/`Succeeded`였다. 초기 단독 컴파일에서 드러난 로비 서브시스템의 기존 `UPackage` include 누락을 필요한 범위에서 보완했다. 마지막 인증 안내 문구도 [해당 CPP 컴파일](../Saved/BuildChecks/LobbyCharacterConfiguration-20261025/Compile-Final.log)이 통과했다. 당시 열린 공유 에디터의 Live Coding 세션 때문에 마지막 산출물은 임시 위치이며 DLL 반영·핫리로드를 수행한 결과가 아니다. 이 단계의 모듈 링크·실제 화면 입력·HTTP 왕복·재인증/충돌·플레이 검증은 미실행이다. backend API/스키마와 콘텐츠 에셋은 이번에 변경하지 않았다. 선택 UI가 생겼다는 사실은 미작성인 고유 능력·트라이포드 실행 효과를 보장하지 않는다. 다음 구현은 해당 콘텐츠와 첫 구간의 훈련/보스/보상 정의 연결이다.

### 7.1 실제 로비 왕복과 캐릭터별 구성 분리 — 2026-10-26

레벨·트라이포드·슬롯 편집과 변경 취소 뒤 상위 로비도 즉시 갱신하도록 보완했다.
미저장 변경이 생기면 캐릭터 전환 버튼이 바로 잠기고 사유를 표시하며, 취소하면 다시 풀린다.
기존 서버 승인·미확정 명령·저장 정책과 Pawn/GAS 상태의 원본은 유지한다.

기존 `Scripts/Tests/Run-LobbyLogoutE2ETest.ps1`의 `-ConfigurationOnly`로 실제
`LobbyMap`·기본 위젯·UE HTTP를 실행했다. 별도 Development/Memory 백엔드에 같은 계정의
캐릭터 둘과 다른 계정의 캐릭터 하나를 만들었다. 검사 백엔드에만 훈련 메타데이터를 등록하고
기존 출발·서버 인증·정산 API로 첫 캐릭터의 경험치 100·SP 3·습득 스킬 3개를 준비했다.
훈련 맵이나 UE 전투 서버를 실행한 결과는 아니며, 실제 서비스의 콘텐츠 등록을 변경하지 않는다.

- 실제 조회 버튼과 동적 선택 컨트롤로 SP 초과 거절, 4레벨 선택 활성·7레벨 잠금, 레벨 하락 시 선택 해제와
  레벨 복구 뒤 미선택 유지, 변경 취소, 8슬롯 교환·빈칸 유지, 편집/저장 중 전환 잠금을 확인했다.
- 차지 4레벨·`FullCharge`, 슬롯 1의 차지/슬롯 2의 돌진/슬롯 8의 회전을 저장하고 다시 조회했다.
  저장 중 강제 중복 디스패처와 편집은 차단됐고, 별도 HTTP 조회에서 revision `2 → 3` 한 번과
  사용 SP 3·정확한 선택/8슬롯을 확인했다. 취소나 재조회는 추가 저장을 만들지 않았다.
- 같은 계정의 다른 캐릭터는 원래의 SP 0·습득 2개·기본 슬롯을 유지했다. 원래 캐릭터로 돌아와
  다시 읽어 저장 구성을 확인했고, 다른 계정의 성장 기록·두 계정의 목록과 별도 토큰도 보존됐다.

실행 명령은 `-ConfigurationOnly -BackendPort 3050 -TimeoutSeconds 600`이며 UE/러너 종료 0,
기동/검사 이후 Error 각각 0, DLL/선택 소스 전후 해시 일치다. 모듈은 `69DAEA54…B73C1CDF`다.
근거는 `Saved/Tests/LobbyConfiguration/7917036157134f619fc37f144799cbd7/`의
`Verification.proof.json`과 `ConfigurationState.proof.json`이다. 정상 Editor 타깃은
60개 작업·DLL 링크/메타데이터·종료 0이며
`Saved/BuildChecks/LobbyConfiguration-20261026/Editor-Retry.log`와 `NormalBuild.json`에 기록했다.
첫 빌드의 공유 전투 헤더/UHT 생성 파일 불일치는 `Editor-Build.log`에 남겼고,
생성 파일 갱신 뒤 정상 빌드를 완료했다.

검사 종료 뒤 공유 DLL이 `53509761…F7961F08`로 갱신됐다. 검사한 선택 소스 23개는 이후에도
일치했지만, 위 실행 성공은 전후 해시가 일치한 `69DAEA54…B73C1CDF`의 근거로 유지한다.
후속 공유 모듈의 재실행 결과로 바꾸어 표기하지 않는다.

이 결과는 null RHI의 실제 UMG 이벤트와 개발 인증/Memory 왕복이다. 실물 입력·GPU 배치,
실제 Steam·PostgreSQL, 같은 계정 재인증·동시 revision 충돌·저장 응답 유실/복구와
저장 구성의 전투 Pawn/GAS 복원은 별도 검증 대상이다. 이전 7절의 NoLink/미실행 기록은
당시 구현 단계이며 이번 범위의 정상 모듈/로비 HTTP 실행 대기는 해소됐다.

### 7.2 조회 실패의 편집 보존과 미확정 저장 재인증 복구 — 2026-10-26

저장본 조회 실패가 이미 확인한 저장본과 로컬 편집본을 무효화하지 않도록 고쳤다.
특히 조회 401 뒤 같은 계정으로 다시 로그인하면 편집을 이어서 저장할 수 있다.
403/404는 편집본을 보존하면서 새 편집·저장을 잠그고 확인된 재조회를 요구한다.
처음부터 읽은 기록이 없던 상태에 기본값을 만들거나, 미확정 쓰기 명령을 새 요청으로 교체하지 않는다.

기존 러너에 `-ConfigurationOnly -ConfigurationRecovery`를 추가했다.
루프백 전용 `CharacterConfigurationHttpFaultProxy.mjs`가 첫 조회의 UE 토큰을
실제 백엔드 로그아웃 API로 폐기하고, 저장본 다시 읽기의 실제 401을 확인한다.
편집한 차지 4레벨·FullCharge·8슬롯과 같은 캐릭터 선택을 유지한 채 로그인 버튼으로 재인증한다.

이어 실제 PUT의 백엔드 200 응답 전체를 받은 뒤 UE 쪽 연결을 끊었다. UI가 편집본과 원래
Request ID/revision/본문을 보존하고 조회·취소·캐릭터 전환·로그아웃을 잠그는지 확인했다.
해당 쓰기의 토큰도 백엔드에서 폐기한 뒤 재확인 버튼으로 실제 401을 받고, 같은 계정으로
두 번째 재인증한 후 원래 명령을 재전송했다. 프록시는 바이트 단위로 같은 명령만 통과시키며
원본과 재전송 200 응답의 SHA-256도 같았다. 연결 유실 뒤 자동 재전송은 관측되지 않았다.

백엔드 쓰기 요청은 최초 200·폐기된 토큰의 401·멱등 재전송 200의 세 번이며,
저장 revision은 `2 → 3` 한 번만 증가했다. 재조회·같은 계정의 다른 캐릭터 선택·원래 캐릭터로
돌아온 저장 구성과 다른 계정/별도 토큰·목록 보존도 기존 흐름으로 확인했다.
프록시 증거에는 자격증명과 원래 쓰기 본문 대신 요청 ID·명령/응답 해시·관측 횟수를 기록한다.

실행은 `-ConfigurationOnly -ConfigurationRecovery -BackendPort 3052 -TimeoutSeconds 600`이다.
UE/러너 종료 0, 기동/실행 Error 각각 0, DLL/선택 소스 24개 전후 해시 일치이며
검증 모듈은 `3C1CD079…1D75CCBA`다. 근거는
`Saved/Tests/LobbyConfigurationRecovery/5069e6832d184f53a44b690291cff170/`의
`Verification.proof.json`, `ConfigurationRecovery.proxy.proof.json`, `ConfigurationState.proof.json`이다.
프록시 집중 검사 3개도 통과했다: 동일 명령/변경된 인증의 복구, 변조 재시도 차단,
실패한 upstream 응답을 커밋 유실로 세지 않는 경계다. backend Release 빌드는 경고/오류 0이었다.

자체 정상 Editor 타깃 확인은 최신 공유 모듈에서 0 action·종료 0이다.
`Saved/BuildChecks/LobbyConfigurationRecovery-20261026/Editor-Build.log`와 `NormalBuild.json`에
자체 추가 컴파일/링크 없이 공유 산출물을 검증한 범위를 구분했다.
이 실행은 개발 인증/Memory·null RHI의 실제 UMG 이벤트/UE HTTP다. 실제 Steam·PostgreSQL·
실물 입력/GPU, 동시 revision 충돌·403/404 조회 UI·프로세스 재시작 복구와 전투 Pawn 적용은
이번 범위에 포함되지 않는다. 앞선 7.1의 재인증/응답 유실 미검증 중 이번 두 경로는 해소됐다.

### 7.3 별도 접속의 선행 저장과 revision 충돌 복구 — 2026-10-26

충돌 안내에 이번 저장이 거절됐고 편집본은 보존됐음을 명시했다. 재조회가 필요한 상태의 버튼은
**최신 저장본으로 다시 시작**으로 표시한다. 확인된 409는 미확정 쓰기와 구분해 원래 명령을
종료하고, 최신 저장본 조회와 명시적 재편집 전까지 새 편집·저장·취소를 잠근다.
7.2의 커밋 미확정 재전송은 원래 ID/본문을 유지하며, 이번 확인된 충돌 후 새 의도에는 새 ID를 쓴다.

기존 러너의 `-ConfigurationOnly -ConfigurationConflict`로 다음 실제 경로를 확인했다.
별도 플레이어 토큰의 HTTP 접속은 같은 Steam 계정의 두 번째 접속 역할이며, UE 클라이언트는 하나다.

1. UE가 revision 2를 읽어 FullCharge·슬롯 교환을 편집한 뒤 별도 접속이 QuickCharge·기존 슬롯을
   정상 PUT으로 저장해 revision 3을 만든다. 프록시가 409를 만들어 주지 않는다.
2. UE의 이전 revision 2 저장이 백엔드의 409 `stale_progression_revision`으로 거절된다.
   기존 편집/선택/안내와 재조회 버튼은 유지되고, 강제 편집·저장 디스패처도 차단된다.
   이 시점의 별도 HTTP 조회가 선행 저장 결과 전체와 같아 덮어쓰기가 없음을 확인했다.
3. 명시적으로 다시 읽으면 QuickCharge·원래 슬롯의 revision 3이 표시된다. 다시 FullCharge·슬롯을
   편집하고 새로운 Request ID·expected revision 3으로 저장해 revision 4를 확인했다.
   이후 재조회·다른 캐릭터 선택·돌아온 저장본과 다른 계정/별도 토큰 보존도 통과했다.

프록시가 관측한 UE 쓰기는 거절 1회·성공 1회이며, 별도 접속의 선행 저장은 따로 검증했다.
근거는 `Saved/Tests/LobbyConfigurationConflict/74da8273de0944aa875dd9b320469319/`의
`Verification.proof.json`, `ConfigurationConflict.proxy.proof.json`, `ConflictRejectedState.proof.json`,
`ConfigurationState.proof.json`이다. UE/러너 종료 0·기동/실행 Error 각각 0,
DLL/선택 소스 24개 전후 일치이며 실행 모듈은 `8E2F2A1A…D01D5B44`다.
집중 프록시 5개와 backend Release 경고/오류 0도 확인했다.

정상 Editor 후속 확인은 최신 공유 모듈에서 0 action·종료 0이다. 자체 첫 컴파일의 UE_LOG 분기
오류를 수정하고 공유 보스 헤더/생성 파일 불일치 후 재생성한 기록은
`Saved/BuildChecks/LobbyConfigurationConflict-20261026/Editor-Build.log`·`Editor-Retry.log`에 구분했다.
첫 실행은 별도 접속 요청의 기본 JSON 깊이가 트라이포드 배열을 문자열로 바꿔 준비 단계에서
실패했다(`Saved/Tests/LobbyConfigurationConflict/14c02d23e7da4b1b97608e126bf2c09f/`).
검사 도구의 깊이를 8로 명시한 뒤 위 실행을 완료했으며 게임 API 계약은 변경하지 않았다.

이 검사는 개발 인증/Memory·null RHI의 실제 UMG 이벤트/HTTP와 두 접속 의도의 순서 경합이다.
PostgreSQL 동시 SQL·다른 PC/실제 Steam·실물 입력/GPU·403/404 조회 UI·전투 Pawn 적용 및
프로세스 재시작 복구 검증은 별도다. 앞선 7.2의 revision 충돌 미검증 중 이 경로는 해소됐다.

### 7.4 성장 조회 거절과 생성 직업 목록 실패/재시도 — 2026-10-26

조회 403/404 안내에 저장 잠금과 재조회 방법을 명시하고, 보존한 성장 요약은 **이전 조회 정보**로
표시한다. 읽은 기록이 없는 상태에는 최신 저장본으로 다시 시작하는 문구를 표시하지 않는다.
실패한 조회가 캐릭터·편집본을 초기값으로 대체하지 않으며, 확인된 재조회 후에만 편집을 다시 허용한다.

기존 러너의 `-ConfigurationOnly -ConfigurationReadFailures`에서 다음 로비 흐름을 실행했다.

- 생성 직업 목록의 검사 응답 503 → 실제 목록 재조회 → 검사 응답 `200 []` → 실제 목록 재조회.
  실패/빈 목록에서는 선택/생성을 잠그고 강제 생성·중복 새로고침도 기존 가드로 거절했다.
  이름 `KeepDuringRetry`와 소유 캐릭터 2개는 유지됐고, 캐릭터 생성 POST는 0건이었다.
- 성장 편집 후 프록시가 해당 GET을 같은 UE 토큰으로 다른 계정의 캐릭터/없는 GUID에 중계해
  실제 백엔드의 소유권 403·존재 검사 404를 받았다. 선택/편집본을 보존하고 편집·저장을 잠갔으며
  강제 편집/저장도 거절했다. 별도 HTTP 조회로 저장된 성장 기록 전체가 그대로임을 확인했다.
- 원래 캐릭터의 GET으로 복구한 뒤 확인한 저장본을 읽고 다시 편집·저장했다. revision `2 → 3`
  한 번과 재조회·다른 캐릭터/계정·목록/별도 토큰 보존을 확인했다.

목록 오류/빈 응답은 로컬 검사 도구가 제공한 장애이며, 403/404는 위 대상 변경으로 백엔드가
실제 반환한 응답이다. 선택한 캐릭터의 소유권이나 저장 행을 실제로 삭제/이전한 검사는 아니다.
검사용 훈련 메타데이터와 개발 인증/Memory 준비는 앞선 실행과 같은 경계를 따른다.

실행은 `-ConfigurationOnly -ConfigurationReadFailures -BackendPort 3056 -TimeoutSeconds 600`이다.
UE/러너 종료 0·기동/실행 Error 각각 0·DLL/선택 소스 24개 전후 일치이며,
모듈은 `ACDF7D10…D2CD3390`이다. 근거는
`Saved/Tests/LobbyLookupFailures/9958804e8cb7493dbea482901aa1d21f/`의 `Verification.proof.json`,
`LookupFailures.proxy.proof.json`, `FailedReadsState.proof.json`, `ConfigurationState.proof.json`이다.
집중 프록시 8개와 backend Release 경고/오류 0도 확인했다.
자체 정상 Editor 후속은 최신 공유 모듈에서 0 action·종료 0이며
`Saved/BuildChecks/LobbyLookupFailures-20261026/Editor-Retry.log`·`NormalBuild.json`에 기록했다.
초기 타깃의 별도 인벤토리 검사 private 호출 오류는 갱신된 호출 코드 반영 후 해소됐으며,
이 작업에서 인벤토리의 접근 범위를 변경하지 않았다.

이 결과는 null RHI의 UMG 이벤트/UE HTTP이며 실제 Steam·PostgreSQL·GPU/실물 입력·앱 재시작
복구나 전투 Pawn 적용 검증과 구분한다. 다음은 저장한 직업·습득·SP/선택·8슬롯의 서버 Pawn/ASC
복원과 실제 사용 연결이다. 이전 실행의 미검증 표시는 각 당시 범위로 유지한다.

### 7.5 로비 편집본의 스킬 수치 미리보기 — 2026-10-28

로비 구성 화면에서 습득 스킬의 레벨/트라이포드 아래에 **기본 상태 미리보기**를 추가했다. 실제 카탈로그의 Definition과 현재 편집본의 안정 ID를 기존 RuntimeSpec으로 해석한다. 별도 밸런스 표를 UI에 복사하지 않는다.

- 선택 후 쿨타임과 피해 배율, 완충 시간/최대 유지 시간, 이동 속도/거리, 반경과 무력화/부위 파괴 기여를 해당 데이터가 있을 때 표시한다. 회전의 종료 타격 기여 조건도 표시한다. 시간은 기존 실행의 ChargeTime/HoldTime 배율을 반영하며, 피해는 최종 HP 피해가 아닌 기본 피해 대비 배율이다.
- 레벨/선택 변경과 변경 취소는 현재 편집본으로 문구를 갱신한다. 미리보기만으로 저장·SP 사용·GAS 부여·쿨타임/자원을 적용하지 않는다. 저장 후 다음 전투 입장에 반영된다는 툴팁을 둔다. 기존 인증/리스/미확정 저장·충돌/캐릭터 전환 규칙은 유지한다.
- 카탈로그는 기존 GameInstance 서브시스템에서 비동기로 읽고 공유 로딩을 재사용한다. 콜백은 특정 캐릭터/이전 선택을 캡처하지 않고 현재 화면의 현재 편집본만 다시 표시하며, 위젯 해제/월드 종료에는 갱신하지 않는다. 요청은 실제 편집 행이 생긴 뒤 시작한다.
- 알 수 없는 ID, 잠긴 선택, 다른 스킬/범위 밖 레벨과 누락된 콘텐츠는 기본값으로 바꿔 표시하지 않는다. 로딩 또는 확인 불가 안내를 표시하며, 콘텐츠 조회 실패 때문에 기존 저장 권한을 확대하거나 로컬 편집본을 고치지 않는다. `GetSkillPreview(SkillTag)`는 같은 읽기 전용 문구를 커스텀 UI에 제공한다.

작업 범위는 `RPGCharacterConfigurationWidget.h/.cpp`와 별도 `RPGCharacterConfigurationPreview.h/.cpp`, 새 집중 검사 파일이다. 작업 조율표에 먼저 예약했고 다른 담당의 가방/장비 WBP·회전 실행/정의·기여 수신기·스킬 에셋은 수정하지 않았다.

증거는 `Saved/BuildChecks/LobbySkillPreview-20261028/ImplementationResult.json`에 연결한다. 초기 컴파일의 지역 변수 이름 충돌을 수정한 뒤 `Compile-Fixed.log`의 관련 CPP 2개 `-NoLink` 증분이 성공했다. 앞선 82개 작업에서 필요한 나머지 객체를 컴파일했고, 정상 UBT `-SkipBuild`로 현재 전체 링크 응답을 생성한 뒤 OUT/PDB만 별도 검증 프로젝트로 바꿔 게임 DLL 링크를 성공시켰다. 공유 DLL·에디터는 교체/종료/핫리로드하지 않았다. 이는 정상 Editor 전체 타깃 링크 성공과 구분한다.

별도 DLL의 `Native.log`/`NativeReport`에서 **3개 Success·종료 0**을 확인했다. 실제 네 스킬 에셋으로 돌진의 8/7/6/5초·거리 조합, 차지 1.2/0.7초, 도약 25/25 기여와 회전 End 조건, 입력 불변성과 미지/잠긴 선택·범위 밖 값 거절을 확인했고 기존 로비 위젯 생성 회귀를 포함했다. 기존 기동 `Condition failed` Error 15줄은 테스트 탐색 이전이며 이후 Error는 0이다. 읽기 전용으로 고정한 스킬 소스/에셋 11개와 원래 공유 DLL은 변경되지 않았다.

실제 저장 캐릭터의 HTTP/UMG 선택·지연 로딩·캐릭터 전환/취소 뒤 표시, GPU/물리 입력은 이번에 재실행하지 않았다. 코드/읽기 전용 모델과 기존 로비 위젯 생성 검증이며, 실운영 화면 검증으로 확대하지 않는다. 실행 중인 원본 에디터에는 아직 반영하지 않았고 다음 조율된 정상 빌드/재실행에서 적용한다.

### 7.6 미리보기의 실제 로비 선택·저장·캐릭터 분리 — 2026-10-29

7.5의 미리보기를 기존 `-ConfigurationOnly` 실제 로비 검사에 연결했다. 제품의 저장/인증/스킬 실행 코드는 바꾸지 않고 `RPGDungeonLobbyConfigurationE2EProbe.cpp`와 `Run-LobbyLogoutE2ETest.ps1`만 확장했다. 숫자 해석기 단독 호출뿐 아니라 실제 WidgetTree의 표시 행과 현재 편집본 문구를 비교한다.

- 실제 선택 상자로 `FullCharge` 1.2초 → `QuickCharge` 0.7초 → 원래 선택을 확인하고, 레벨 변경·변경 취소 후 표시를 확인했다. 레벨 1의 기본 차지도 충전형일 수 있으므로 형식 유무 대신 해당 캐릭터의 기본 레시피와 비교한다.
- 편집 중에는 저장하지 않고, 명시적인 저장 뒤 차지 4레벨/`FullCharge`·8슬롯을 HTTP로 다시 확인했다. revision은 `2 → 3` 한 번, 사용 SP는 3이다. 저장본 재조회와 저장 후 변경 취소에도 표시가 일치한다.
- 같은 계정의 다른 캐릭터는 습득 2개·기본 선택/표시를 유지한다. 전환 후 저장본을 읽기 전에는 이전 캐릭터의 수치 행이 없어야 하며, 원래 캐릭터로 돌아와 다시 읽으면 저장한 표시가 복원된다. 별도 계정의 기록도 유지됐다.

러너에 선택적인 `-ProjectFile`과 `-BackendBuildDirectory`를 추가했다. 기본 호출은 기존 동작을 유지한다. 이번에는 별도 Editor 프로젝트/DLL과 7.1절에서 검증한 닫힌 backend 빌드의 복사본을 사용했다. 다른 담당의 활성 backend 소스를 빌드하거나 공유 DLL·에디터·MCP·아이템 에셋을 바꾸지 않았고, 자체 Memory backend TCP 3061/독립 UserDir만 사용했다. 현재 backend 소스의 새 빌드/검증 결과로 해석하지 않는다.

`Saved/BuildChecks/LobbySkillPreviewLive-20261029/ImplementationResult.json`이 최종 근거다. `Compile.log`의 137개 `-NoLink` 작업과 수정 probe 1개의 `-FileList -NoLink` 컴파일이 성공했다. 정상 UBT 전체 링크 응답의 OUT/PDB를 분리하고 **수정 probe는 실제 `SingleFile/…cpp.obj`를 참조**한 `Isolated-Link-Final.rsp`로 별도 DLL 링크를 성공시켰다. 정상 Editor 전체 타깃 링크/공유 모듈 적용은 아니다.

최종 실제 실행은 `Saved/Tests/LobbyConfiguration/736888356ab44a96a3afae99a60a2f15/`의 `Verification.proof.json`, `ConfigurationState.proof.json`, `client.log`다. UE/러너 종료 0, `LOBBY_SKILL_PREVIEW_E2E PASS`, 기동/실행 Error 각각 0, 선택 입력 36개와 실행 DLL 전후 일치다. 추가로 읽기 고정한 16개 입력·원본 공유 DLL도 그대로다. 앞선 두 실패는 삭제하지 않았다. 첫 실행 `b0776c…`의 잘못된 기본 차지 가정과 두 번째 `6daf36…`의 이전 일반 객체 재링크를 고친 뒤 최종 실행만 성공으로 기록한다.

이는 null RHI의 실제 UMG 이벤트·개발 인증/Memory HTTP 범위다. GPU 배치/실물 입력, 강제로 지연시킨 카탈로그 콜백·화면 해제 경합, 전투 Pawn/GAS 적용, Steam·PostgreSQL·Shipping은 추가 확인하지 않았다. 원본 에디터에 적용하려면 조율된 정상 빌드·재실행이 필요하다.

## 8. 캐릭터 목록의 저장 성장 요약 — 2026-10-26

인증된 플레이어의 기존 `GET /api/characters` 각 행에 선택적인 `progression` 객체를 추가했다.
기존 `characterId`, `rosterId`, `name`, `createdAt`은 유지하며 응답은 `Cache-Control: no-store`다.

```json
"progression": {
  "state": "Available",
  "classId": "RPGPlayerClassDefinition:GreatSwordWarrior",
  "level": 1
}
```

- `Available`: 해당 캐릭터의 검증된 저장 기록에서 직업 ID와 캐릭터 레벨을 계산한다.
- `Missing`: 저장 행이 없다. 직업/레벨은 null이며 초기 기록을 생성하지 않는다.
- `Unavailable`: 저장의 소유 ID·프로필·직업·수치/구성이 유효하지 않거나 JSON이 손상됐다.
  다른 캐릭터의 값이나 신규 캐릭터의 기본값으로 대체하지 않는다.

Memory와 PostgreSQL 모두 인증된 SteamID 소유의 행만 읽는다. PostgreSQL은 소유 캐릭터와
성장 행의 LEFT JOIN 한 번으로 요약을 읽고, 한 캐릭터의 잘못된 기록은 해당 요약만
확인 불가로 만든다. 기존 목록 조회와 요약 조회 외에 캐릭터마다 별도 쿼리를 보내지 않는다.
서버/관리자 자격으로 이 플레이어 목록을 읽는 권한을 추가하지 않았다.

요약은 저장 상태의 읽기 전용 투영이다. 미반영 정산의 XP·해금 지급, 기록 초기화,
DB 마이그레이션·revision 갱신을 하지 않는다. 알려진 초안 직업 ID는 표시 ID만 정규화하고
원본은 바꾸지 않는다. 상세 성장 GET·구성 저장·입장 시의 기존 정산 반영과 검증은 그대로다.
`POST /api/characters`는 기존 생성 트랜잭션을 유지하며 응답의 요약은 null일 수 있다.
로비는 생성 완료 뒤 목록을 새로 읽어 실제 저장 요약을 받는다.

UE는 요약이 없는 구형 응답도 캐릭터 신원 정보로 처리하되 직업/레벨을 확인 불가로 표시한다.
`Available`의 올바른 직업 ID 형식과 양의 정수 레벨만 표시용 필드로 수용한다. 이 요약은
습득/서버 복원이나 저장 명령의 원본이 아니며 기존 전체 성장 레코드 검증을 대체하지 않는다.

검증: Release 빌드 경고/오류 0, 기존 StarterEquipment/ProgressionCases와 요약 소유 범위·
형제 캐릭터 독립 레벨·누락/손상/미지원/불일치·목록 조회의 미지급/무변경 사례가 통과했다.
실제 Memory/개발 인증 HTTP에서는 계정별 2명/1명 목록, 저장 요약, no-store, 다른 계정의
성장 403과 미인증 목록 401을 확인했다. PostgreSQL 구현은 컴파일했지만 런타임 검사는
하지 않았다. 증거와 UE 컴파일 범위는 [로비 표시 기록](../Docs/ONLINE_LOBBY_UI.md#캐릭터별-저장-직업레벨-표시-2026-10-26)에 있다.

## 9. 콘텐츠 후속 — 2026-10-26

7절의 UI 구현 이후 현재 에디터의 모듈에 새 네이티브 타입이 로드된 것을 확인해 대검의 고유 능력/입력을 실제 클래스 에셋에 저장했다. 차지의 4레벨 `FullCharge`/`QuickCharge`도 기존 옵션을 보존하며 실제 Definition에 추가했다. 이 두 저장 ID는 이제 해당 해금 티어·옵션으로 해석할 수 있다. 클래스 미설정과 해당 두 ID 누락은 해소됐으며 7절의 콘텐츠 미변경 기록은 그 당시 범위다.

옵션 시간·상대 피해·조기 취소와 그대로 유지한 기본값은 [스킬 콘텐츠 계약](../Docs/SKILL_AND_TRIPOD_MODULAR_ARCHITECTURE.md#2026-10-26-투지-해방-바인딩과-차지-4레벨-선택-저장)을 따른다. 이번 확인은 에셋 저장·읽기와 작성 함수의 반복 무변경이다. 실제 HTTP/Pawn/GAS/복제 성공이나 표의 나머지 선택 효과 완료를 뜻하지 않는다. backend API·스키마는 바꾸지 않았다.

그 뒤 [범위 배율 소비 경로](../Docs/SKILL_AND_TRIPOD_MODULAR_ARCHITECTURE.md#2026-10-26-범위-배율의-실제-hitquery-연결)를 UE 소스에 연결하고, 정상 DLL의 새 API 로드 뒤 [도약 `WideArea`/`ExtraArea`를 실제 Definition에 저장](../Docs/SKILL_AND_TRIPOD_MODULAR_ARCHITECTURE.md#2026-10-26-도약-범위-선택-실제-저장)했다. 해당 두 ID는 각각 4/7레벨 티어로 해석할 수 있다. 기본 반경 대비 ×1.2/×1.1, 조합 ×1.32이며 기존 backend 허용 ID·API·스키마 변경은 없다. 이번 확인은 에셋 저장/읽기·기존 속성 보존·반복 작성 무변경이고 실제 HTTP/Pawn 복원이나 피해 검증은 아니다. 아직 효과가 없는 다른 ID를 우선 등록해 복원 검사를 우회하지 않는다.

회전도 [7레벨 `WideArea`를 실제 Definition에 저장](../Docs/SKILL_AND_TRIPOD_MODULAR_ARCHITECTURE.md#2026-10-26-회전-베기-7레벨-범위-선택-저장)했다. 이어 2026-10-27 정상 DLL의 새 설정 로드 후 [4레벨 `Mobile`/`Stationary`를 실제 저장](../Docs/SKILL_AND_TRIPOD_MODULAR_ARCHITECTURE.md#2026-10-27-회전-4레벨-이동-선택-실제-저장)하고 새 프로세스 재로드로 확인했다. 기존 `WhirlWind`·해금 레벨·허용 ID/API/스키마는 유지했으며 두 ID는 이제 실제 옵션으로 해석할 수 있다. `FinalStagger`의 기여 효과 연결은 남아 있다. 이 에셋 저장 확인과 해당 새 선택의 실제 HTTP/Pawn 복원 성공은 구분한다.

돌진도 [4레벨 `LongReach`·7레벨 `ExtraReach`를 실제 저장](../Docs/SKILL_AND_TRIPOD_MODULAR_ARCHITECTURE.md#2026-10-27-돌진-거리-선택-연결과-실제-저장)했다. 기존 `AssultBlade` 저장 태그와 허용 ID·API/스키마를 유지하며 기존 시전 방식 선택 인덱스는 보존했다. 해당 두 ID는 이제 실제 옵션으로 해석할 수 있다. `QuickApproach`/`ExtraRecovery`는 1초 최소 쿨타임과 설계 정리 전이라 미작성이다. 이 에셋 재로드 확인은 새 선택의 실제 구성 HTTP/Pawn 복원 검증과 구분한다.

## 10. 생성 가능한 직업과 선택 생성 — 2026-10-26

인증된 플레이어의 `GET /api/character-creation-options`는 `classId`, `displayName`,
`description` 배열을 반환하며 `Cache-Control: no-store`다. 현재 저장소의 초기 성장/시작
장비 생성이 구현된 `RPGPlayerClassDefinition:GreatSwordWarrior` 한 항목만 제공한다.
미인증은 401, 관리자·서버 등 플레이어가 아닌 자격은 403이다.

새 로비는 해당 목록에서 선택한 ID를 기존 생성 API에 함께 보낸다.

```json
{"name":"새 캐릭터","classId":"RPGPlayerClassDefinition:GreatSwordWarrior"}
```

`POST /api/characters`는 생성 전에 정확한 허용 ID인지 확인한다. 명시적인 빈/공백 ID,
초안 별칭 `RPGPlayerClassDefinition:GreatSword`, 대소문자 차이와 미지 ID는 400
`unsupported_character_class`이며 이름 예약·캐릭터/성장/시작 장비 생성을 하지 않는다.
현재 허용된 선택은 기존 대검 생성 트랜잭션에 연결되며 기존 캐릭터의 직업을 변경하지 않는다.
이 목록에 직업 이름만 추가해 새 직업을 지원하는 것은 아니며 실제 생성/성장/장비 경로를
구현한 직업만 제공해야 한다.

구형 `{"name":"이름"}` 및 null classId는 기존의 명시적인 서버 기본값인 대검 생성을
유지한다. 이름 길이/trim/중복 이름 검사와 트랜잭션 계약은 그대로다. 새 UI는 조회 실패나
선택 부재를 이 호환 경로로 바꾸지 않는다. 새 클라이언트보다 백엔드를 먼저 반영한다.

UE는 전체 목록의 ID 형식·중복 ID/표시 이름·길이를 검증하며 잘못된 일부를 임의로 골라
사용하지 않는다. 실패/빈 목록은 새 캐릭터 생성을 잠그고, 기존 캐릭터의 선택/플레이 자격은
기존 서버 경계로 판단한다. 목록은 생성용 메타데이터이며 미습득 스킬 부여나 저장 복원의
증거가 아니다.

Release 빌드 경고/오류 0, 기존 시작 장비/성장 및 허용 ID 사례가 통과했다. 실제 Memory/
개발 인증 HTTP 17개는 제공 목록과 선택 생성, 구형 호출, 잘못된 ID 5종의 거절·목록 무변경·
이름 미소모, 미인증/다른 계정/관리자 권한 거절을 확인했다. 기존 PostgreSQL 생성 코드는
컴파일했지만 실제 DB 실행은 하지 않았다. 증거·UE 컴파일/실행 범위는
[로비 진행 기록](../Docs/ONLINE_LOBBY_UI.md#서버가-허용한-직업의-캐릭터-생성-2026-10-26)에 있다.
