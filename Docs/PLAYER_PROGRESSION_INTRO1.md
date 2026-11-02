# 입문 1: 접근 실습 공간

작성일: 2026-11-02. **맵·목표 Pawn·완료 GameMode·서버/보상 정의 연결과 재로드 완료.**
[첫 성장 구간](PLAYER_PROGRESSION_FIRST_SLICE_V01.md)의
`Progression.Intro1`의 독립 맵은 아래 작성/재로드 확인 범위를 따른다.
후속으로 서버 개인 목표 컴포넌트3파일의 원본 적용·정상 빌드·집중 native3 검증을 완료했다.
Pawn/맵/서버 정의·조작 안내와 backend 개발 입장 프로필은 아래 후속을 따른다. 클라이언트 입장 연결과
인증된 서버의 실제 목표 완료/정산·입장 목록 공개는 후속이다.

2026-11-02 후속: 입문1 완료 GameMode와 기존 훈련 완료 gate의 콘텐츠 adapter를 원본에
적용하고 정상 빌드/집중 native3 검증을 완료했다. 이후 연결 에셋4개도 적용했으며 실제
목표 달성·파티 완료·정산 성공은 에셋 저장/재로드와 구분한다.

## 맵과 공간

맵은 `Content/Maps/Progression/L_ProgressionIntro1.umap`이다. 기본 훈련 맵을
변경하거나 복사하지 않고 엔진 기본 도형만 사용한다. 크기는 60m×40m이며,
바닥/외벽/중앙 가림막을 BlockAll로 배치한다. 가림막 양쪽에 접근로를 두고
네 시작점은 같은 출발 구역에서 +X 방향을 바라본다.

| 구역 | 중심 좌표(cm) | 반경이 아닌 상자 반크기(cm) | 안정적인 Actor Tag |
|---|---|---|---|
| 출발 | (-2400, 0, 140) | (650, 650, 140) | `Progression.Intro1.Zone.Start` |
| 접근 | (300, 0, 140) | (400, 400, 140) | `Progression.Intro1.Zone.Approach` |
| 공격 위치 | (1700, 0, 140) | (700, 700, 140) | `Progression.Intro1.Zone.Attack` |

각 구역에는 낮은 원형 바닥 표시와 번호/영문 표지를 둔다. 엔진 기본 TextRender 폰트에
한글 글리프가 있다고 가정하지 않는다. 실제 한국어 과제/진행 안내는 아래 읽기 전용
HUD에서 표시한다. 공격 위치는 공간 표시이며 공격 가능한 적이나 피해 표적은 아니다.

## 작성과 보존

[CreateProgressionIntro1Arena.py](../Scripts/Editor/CreateProgressionIntro1Arena.py)는
기본 읽기 전용이며 `-RPGCreateProgressionIntro1Arena`를 명시할 때 없는 맵만 만든다.
보고서의 절대 경로는 `RPG_INTRO1_ARENA_REPORT` 환경 변수로 지정한다.
기존 맵은 소유 메타데이터 `FirstSlice.Intro1Arena.v1`, 배우/좌표/태그/충돌/시작점/표지를
대조하고 저장하지 않는다. 미저장 변경·다른 소유/배치·검사 중 나타난 파일은 보존하고 실패한다.

미연결 초기 맵은 Project_RPG 모듈 없이 독립 content-only uproject로 생성할 수 있었다.
연결된 맵의 읽기에는 호환되는 Project_RPG 모듈과 아래 BP가 필요하다. 작성기는 preview
모드 또는 정확한 소유 Intro1 GameMode·메타데이터·경험 선택자만 인정하고 다른 설정을
덮어쓰지 않는다. 현재 로비 목록·backend 카탈로그·경험치 규칙·입장 권한은 변경하지 않았다.

## 후속 서버 연결

서버 개인 목표는 원본 밖에서 작성한 뒤 공유 컴파일 입력 해제를 확인해 원본에 적용했다.
다음 Pawn/완료 GameMode 연결도 최신 담당 기록과 실제 호출 코드를 다시 확인한다.
기존 성장 규칙의 훈련 완료 선행 조건과 Intro1 완료 후 도약 해금을 재사용하며
별도 진행/경험치 저장소를 만들지 않는다.

구역의 겹침과 태그는 완료 영수증이나 습득/입장 권한이 아니다. 후속 목표는 인증된
Intro1/Normal 배정·현재 캐릭터/Pawn/복원 세대·멤버십과 승인된 행동을 서버에서 확인해야 한다.
미입장/재접속 대기자·확정 포기자와 파티 집계는 기존 계약을 따르고, 잠긴 도약을
Intro1의 필수 조건으로 요구하지 않는다. 실제 목표/적 표적·완료 보고·보상 정의·입장 안내를
연결한 후에만 콘텐츠를 공개한다.

## 훈련→입문1 backend 개발 프로필 — 2026-11-02

`DungeonContent:DevelopmentProfile=Intro1`(환경 변수 `DungeonContent__DevelopmentProfile`)을
명시하면 패키지의 `DevelopmentProfiles/Intro1.json`을 한 번 읽어 아래 두 조합만 허용한다.
기존 Training 프로필과 같은 Development/Testing 전용·출발 최소1/최대1인 개발 범위다.

| 콘텐츠 | 난이도 | 할당 맵 |
|---|---|---|
| `Progression.Training` | `Normal` | `/Game/Maps/Progression/L_ProgressionTraining` |
| `Progression.Intro1` | `Normal` | `/Game/Maps/Progression/L_ProgressionIntro1` |

프로필을 생략한 기존 목록/파티 설정과 기존 Training 전용 프로필은 유지한다. 인덱스형
설정 배열과 합치지 않으므로 기존 보스·입문2·Hard가 섞이지 않는다. 닫힌 이름2개만
해석하고 경로/대소문자 별칭·누락/잘못된 파일·Production/Staging은 거절한다. 실행 중
파일을 바꿔도 이미 만든 카탈로그는 바뀌지 않는다. 인증·저장소·보상·영속 성장 정책은
이 프로필이 바꾸지 않는다.

기존 생성/참가 API의 캐릭터별 선행 진행 검사를 그대로 사용한다. 새 캐릭터는 입문1
생성이 거절되며 훈련의 실제 완료 정산이 성장에 반영된 해당 캐릭터만 입문1을 생성할 수
있다. 같은 계정의 다른 캐릭터는 여전히 잠긴다. 입문1 Normal 완료의 기존 경험치100·
성장 광석2개·도약 해금을 재사용하고 새로 습득한 스킬을 자동 편성하지 않는다.

닫힌 backend/검사/schema 입력117개로 사설 Release 빌드·실제 middleware/Memory HTTP를
실행해 기존 Training7그룹과 Intro1 구성/HTTP7그룹, 총14그룹을 통과했다. 빌드/실행 종료0·
빌드 경고/오류0·사본 입력 불변이며 관리자 출발/claim 전후의 맵·세션/서버 bearer 격리,
훈련 이전 거절/이후 허용·다른 캐릭터 격리·정산 후 총XP200/Lv3/도약·광석2·반복 조회
중복 성장0을 확인했다. 서버 완료 보고는 검사에서 인증된 서버 역할의 HTTP로 수행했으며
UE 목표/GAS 완료·입장 티켓 소비/소켓 연결·물리 입력·GPU·PostgreSQL 성공의 증거가 아니다.

원본 UE/Config/에셋·공유 빌드/정상 DLL·기존 DB/서비스는 수정하거나 실행하지 않았다.
자기 사본/obj/DLL/캐시6,977,984 bytes(6.65MiB)를 자동 폐기해 Temp/정리 예외0이며,
작은 근거는 `Saved/BuildChecks/ProgressionIntro1Profile-20261102/CompletionResult.json`에
연결한다. 폐기한 DLL 해시는 당시 증거다.

현재 UE의 `-RPGDungeonProfile=Training`과 `Start-TrainingDevelopment.ps1`은 여전히 훈련
전용이다. 이번 backend 프로필을 게임 화면에서 선택하려면 클라이언트의 개발 목록과
훈련/입문1 두 맵을 순차 할당하는 개발 실행기 연결이 필요하다. 이 연결 후 실제 입문1
목표 완료·정산·로비 복귀를 확인한다.

## 조작 안내 모듈 — 2026-11-02

입문1 전용 `URPGIntroApproachGuideWidget`를 로컬 HUD 수명에 연결했다. 기존 훈련 안내에서
카드/소유 Controller·살아 있는 현재 Pawn 검증/결과 UI와 포커스 회수 수명만 재사용한다.
기존 훈련의4행·추천/키 조회는 유지하며, 입문1 파생형은3행과 정확한 Intro1 결과 scope,
복제 진행 조회·공간 실습 문구만 바꾼다. 공통 HUD에는 직업/진행 판정 분기를 넣지 않았다.

- 파란01 출발 표시 → 02 표시 밖에서 돌진을 시작해 안에서 종료 → 03 표시 안에서
  회전 베기를 시작·정상 종료하는 순서를 표시한다. 서버의 owner 진행만 읽어 `[대기]`/
  `[완료]`와 `0/3`~`3/3`을 표시하며 목표 setter/완료 RPC·스킬 자동 편성은 없다.
- 돌진/회전은 현재 Pawn 퀵슬롯의 SkillTag를 찾아 공통 입력 라벨로 표시한다. 고정 키를
  가정하지 않고 재지정·슬롯 이동을 반영하며, 배치가 없으면 로비의 명시적 편성/저장 후
  재입장을 안내한다. 구역 통과나 타격만으로 완료됐다고 안내하지 않는다.
- 개인3단계 완료는 파티 완료/보상 영수증이 아니다. 같은 계정·캐릭터·세션·Intro1/Normal의
  클라이언트 문맥일 때 기존 결과 위젯을 재사용하고, 정산 확정과 복귀 허용은 기존 Flow가
  재확인한다. 미인증·standalone의 진행 표시만으로 결과/복귀 권한을 만들지 않는다.
- 다른 Pawn/콘텐츠, 비활성 진행·컴포넌트 제거/사망·위젯 해제에서 문구/참조를 지운다.
  진행 카드는 입력을 막지 않으며 결과 UI의 포커스 회수도 기존 수명을 따른다.

Source8(제품6·직접 회귀2)의 정상 반영과 집중 native9 검증을 완료했다. 초기 정상5작업과
viewport 회귀 보완 후 정상6작업 모두 종료0·Source/Plugins 선택 입력1389 불변이다.
최종 모듈 `A2696532…E3DE4AE8`을 닫힌 runtime에서 실제 로드해 입문1 표시/키/수명3,
기존 훈련 표시/키/수명3, HUD 수명2, 실제 player Slate layer의 두 안내 생성/교체/종료1이
모두 Success·검사 오류0·UE 종료0이었다. viewport 모형의 PlayerInput 미설정 경고1이
있으며 나머지8개는 경고0이다. 기존 기동 Error15는 검사 탐색 이전이며
탐색 후 Error0이다. 첫 검사 fixture의 퀵슬롯 타입/API 오류는 기존 표시 계약으로 고쳤다.

검사 진행은 소유자 복제 모형을 주입했으며 실제 서버 목표 판정/OwnerOnly 전송·물리 입력·
GPU 배치·입장/정산 성공을 대신하지 않는다. 실제 viewport 검사는 production HUD가
카드2개를 각각 생성·회수하고 일반 Pawn에서는 둘 다 숨김을 확인했다. 최초8개 실행과
최종9개 실행의 자기 runtime/캐시 사본 최소370,080,255 bytes(352.94MiB)·링크20을 폐기했고
Temp/정리 예외0이다. 원본 Binaries/Intermediate·공유 DDC·타 담당 입력은 보존했다.
근거는 `Saved/BuildChecks/ProgressionIntro1Guide-20261102/CompletionResult.json`과
`348fe36855744bea9e891699a80f561c/Result.json`이다. 폐기한 모듈 hash는 당시 증거다.
backend 개발 프로필 연결은 위 절을 따르며 클라이언트 입장·실제 목표 달성/완료·정산은 후속이다.

## Pawn·GameMode·서버 정의의 실제 콘텐츠 연결 — 2026-11-02

| 원본 에셋 | 연결 내용 |
|---|---|
| `Content/Blueprints/Character/Player/Progression/BP_Intro1Player.uasset` | 기존 BP_Player의 자식, 복제되는 Intro1 목표 컴포넌트1개. 시작 데이터 상속 유지/훈련 목표0 |
| `Content/Blueprints/GameMode/Progression/BP_Intro1GameMode.uasset` | ARPGIntroApproachGameMode 부모, 새 Pawn과 기존 Controller/PlayerState/GameState/HUD 역할, Normal/보상 활성·티켓/Steam 신원 검사 유지 |
| `Content/System/Dungeons/Progression/DA_DungeonServer_Intro1.uasset` | 정확한 Progression.Intro1/Normal → L_ProgressionIntro1·기존 DA_DungeonReward_Intro1 매핑 |
| `Content/Maps/Progression/L_ProgressionIntro1.umap` | 새 완료 GameMode 지정·기존 클라이언트 표현 경험 선택자1 추가, 기존25개 배우/시작점4/구역3·충돌 유지 |

보상은 기존 FirstSlice.v1/성장 광석2/CharacterBound·재화/증표/개인 장비 추첨0 정의를
검사한 뒤 읽기 참조했다. 새 지급 API나 보상을 만들지 않았다. 경험치100·도약 해금은
기존 진행 규칙이 실제 완료 정산에서 결정하며 이 참조 연결을 지급 성공으로 표시하지 않는다.

[CreateProgressionIntro1Content.py](../Scripts/Editor/CreateProgressionIntro1Content.py)는
기본 읽기 전용이다. `-RPGConfigureProgressionIntro1Content`만 누락 에셋3개 생성과 정확한
소유 preview 맵의 연결을 허용한다. 기존 에셋은 소유/부모/역할/보상/복제·dirty·disk hash를
대조하고 저장하지 않는다. 설정이 다르면 보존/실패한다. 절대 결과 경로는
`RPG_INTRO1_CONTENT_REPORT`로 지정한다. Arena 작성기도 연결 완료 맵을 읽기 전용으로
확인할 수 있게 확장했고 새 모드를 임의로 preview 상태로 되돌리지 않는다.

닫힌 Content overlay에서 생성4/반복0/새 Cmd 재로드0과 Arena 읽기0·파일별 동일 hash,
두 프로세스 실제 F5B8F0D2…7500BF99 모듈 경로/해시·종료0/각 Error0·Mode CDO/서버/보상
검증을 확인했다. 최초 시도는 보상 참조를 넣기 전에 모드를 컴파일해 기존 검증에 거절됐고
원본 설치0이었다. 설정 후 컴파일 순서로 보완한 최종 성공과 이력을 분리한다.
원본 입력 hash를 재조회한 뒤 새3개는 overwrite=false, 기존 맵은 백업을 둔 원자 교체로
반영·설치 후 hash도 같았다. 새 맵 hash는 `D2CE1400…0AF05E29`, 소유 배우는26개다.

게임 Source/UBT/UHT·공유 Intermediate/Binaries 쓰기0, 기존 훈련·보상·부모/HUD/설정
쓰기도0이다. 첫/최종 자기 사본·UserDir/캐시/로그·맵 백업 합계121,948,928 bytes
(116.30MiB)와 소유 링크128개를 자동 폐기·Temp/정리 예외0이며 정상 출력/공유 DDC는
유지했다. 최종 에셋4개·작성기2개와 작은 증거만 남겼다. 증거:
`Saved/BuildChecks/ProgressionIntro1Content-20261102/Verification.proof.json` 및
`Create.json`/`Repeat.json`/`Reload.json`/`ArenaReload.json`·초기 `Attempt1`.
이는 에셋 배치의 확인이며 GPU/물리 조작·인증된 입장·목표 승인/복제·파티 정산은 별도다.

## 서버 개인 목표 — 2026-11-02

`URPGIntroApproachObjectiveComponent`는 Pawn에 명시적으로 배치하는 독립 관찰자다.
기존 플레이어 BP/훈련 컴포넌트/공통 스킬·GameMode·성장 저장 코드는 수정하지 않는다.

| 순서 | 서버에서 관찰할 조건 |
|---|---|
| 출발 | 인증된 현재 Pawn이 출발 상자 안에 있음 |
| 접근 | 출발 확인 뒤, 습득/편성·현재 부여 검사를 통과한 돌진이 접근 상자 밖에서 시작해 안에서 정상 종료. 서버 위치의 순이동100cm 이상 |
| 공격 | 접근 완료 뒤, 회전 베기가 공격 상자 안에서 시작하고 안에서 정상 종료 |

도약/피해 적중/투지 해방은 이번 완료 조건이 아니다. 현재 공격 위치는 공간 표시이며
실제 적중 실습이나 자연 전투의 성공으로 일반화하지 않는다. 회전 베기는 훈련 뒤
이미 습득한 스킬이지만 기존 명시적 편성/저장 후 다음 전투 Pawn 복원을 거쳐야 사용한다.

서버는 Intro1/Normal의 확인된 배정·전용 서버/현재 Controller/Pawn/PlayerState·같은
세션/캐릭터/Steam 신원/접속·복원 세대·준비된 클래스/ASC와 회수 여부를 확인한다.
기존 `URPGPlayerClassComponent::CanUseAbilitySpec`으로 습득·편성·부여 소유권을 확인하며
Ability 인스턴스/핸들·고정 RuntimeSpec 태그·Commit·시전 serial·정상 End를 대조한다.
취소되거나 오래된 재사용 인스턴스의 End는 진행을 승인하지 않는다.

구역은 동일 World의 세 TriggerBox를 정확한 태그로 찾는다. 중복/누락·동일 배우의 구역
겸용·잘못된 충돌/빈 크기/비정상 변환을 거절한다. 상자 로컬 좌표로 회전/스케일을 반영하고,
관찰 도중 구역 배우/변환/크기가 바뀌거나 신원·Pawn 수명이 바뀌면 시전/진행을 초기화한다.

진행 원본은 컴포넌트의 임시 `FRPGIntroApproachProgress` 하나다. OwnerOnly 복제와
읽기 전용 조회/변경 알림만 노출하며 클라이언트 완료 RPC·BP setter·경험치/SP/보상 쓰기는
없다. 이 임시 상태는 영속 완료 플래그나 지급 영수증이 아니다. 기존 완료 보고의 명부/
접속 세대·현재 개인 완료 이벤트와 신선한 멤버십 집계를 연결하는 것은 후속 GameMode 범위다.

집중 native는 실제 TriggerBox의 배치/교체/회전·스케일·잘못된 ID, 내부 공간 순서와 경계,
미인증 standalone의 관찰 거절/표면 권한을 확인한다. 순서 검사는 friend fixture로 **공간
판정만** 호출한다. 인증된 GAS 성공 시전/실제 OwnerOnly 네트워크 전송·파티 완료/XP의
검증 근거로 쓰지 않는다.

정상 타깃은 기존과 다른 첫 명령에서 전체 재컴파일을 시작해 본인 UBT만 신원/시작 시각/
부모를 대조해 중단했다. 기존 `NoHotReloadFromIDE/NoLiveCoding/NoUBA` 옵션으로
597작업·링크/타깃 종료0, Source/Plugins 선택 입력1,364 불변·기존 C4996 경고6과
정상 `7554F2EC…BE630010`을 확인했다. 기존 공유 Intermediate/Binaries는 보존했다.

첫 닫힌 실행은 native3 Success/UE0이지만 Process.Modules의 이전 조회가 캐시돼
로드 경로 관측이 비었으므로 실행기의 false를 그대로 보존했다. 실제 테스트 성공과
전체 실행기 실패를 구분한다. 조회를 매번 새 Process로 바꾸고 검사 전용 AssetManager
설정에서 GameFeatureData 규칙을 유지하도록 보완했다. 그 사이 인계받은 후속 정상
`63D4D635…ECFA42F4`의 성공 이력/입력1,380에서 본인3 SHA와 최종 원본3 SHA를 대조해
제품을 재빌드하지 않고 재사용했다. 새 닫힌 실행의 **동일 모듈 실제 로드 경로/해시·UE0·
native3 모두 Success/사례 오류·경고0**, 기동 Error15/발견 이후0을 확인했다.
기동 오류 해결·원본 전체 최신 기능의 실행 성공을 주장하지 않는다.

근거는 `Saved/BuildChecks/ProgressionIntro1Objectives-20261102/Verification.proof.json`,
`BuildResult.json`/`InstalledModule.json`과 최종
`8c407705fed84540a8a6403e909fe598/{Result,NativeReport}.json`·최소 로그다.
첫 중단 빌드/첫 실행기 실패는 별도 보존했다. 닫힌 사본 취득 뒤 전체 공유 입력/출력 예약을
즉시 해제했고 본인3은 최종까지 동일하다. 기존 Intro1 맵 해시/53,114 bytes도 보존했다.

두 실행의 자기 DLL/Config/UserDir/캐시·Temp와 원본 적용된 중복 작성본3개를
**계측 최소369,411,886 bytes(352.30MiB)** 폐기·소유 링크20개를 대상 추적 없이 해제했다.
이미 지운 중복 raw 로그 용량은 미계측/합계 제외다. Temp/정리 예외0·타 프로세스 종료0·
본인 예약 전체 해제이며 정상 Binaries/Intermediate·공유 DDC는 삭제하지 않았다.
폐기한 사본/로그 해시는 당시 증거만 남긴다.

## 기존 파티 완료 gate의 입문1 적용 — 2026-11-02

`ARPGIntroApproachGameMode`는 기존 `ARPGTrainingGameMode`의 집계·보고 수명을 상속한다.
콘텐츠 ID, 해당 Pawn의 목표 컴포넌트 찾기, 활성/빈 상태/완료 읽기와 타입별 delegate 연결/
해제만 override한다. 입문1은 `Progression.Intro1`과 `URPGIntroApproachObjectiveComponent`,
기존 훈련 기본 구현은 종전 ID와 `URPGTrainingObjectiveComponent`를 계속 사용한다.
공통 클래스가 입문1 타입을 참조하거나 새 콘텐츠별 switch로 보상 경로를 갈라놓지 않는다.

개인 목표가 완성됐다는 bool만으로 정산하지 않는다. 기존 gate의 최초 전원 명부·현재
Pawn/Controller/PlayerState·신원/접속/복원/전투 수명, 확정 제외 영수증, 중복/알 수 없는
참가자 거절과 **전원 완료 이벤트 이후 시작된 신선한 멤버십 GET**을 그대로 통과해야 한다.
타이머에서 제출 상태를 먼저 고정한 뒤 기존 부모 GameMode의 고정 보상/재시도 경로를 한 번
호출한다. 멤버십 HTTP 조회·정산 저장소·클라이언트 완료 RPC를 새로 만들지 않았다.

훈련의 기존 `RPG_TRAINING_PARTY_COMPLETE` 표시는 유지하며 공통 진단에는 정확한 콘텐츠 ID를
추가했다. 이미 완료된 객체를 늦게 발견해 완료 이벤트를 만들지 않고, 활성화된 빈 상태에서만
새 관찰 수명으로 재등록하는 동작도 유지한다.

새 native는 훈련16가지/입문1의8가지 부분·완료 상태의 타입 구분, delegate 구독/해제,
미인증 완료 bool의 타임스탬프 제거와 양쪽 공개 clear 진입의 거절을 확인한다. 표시용 상태를
주입한 standalone fixture이며 인증된 전용 서버 전원 완료·정산 성공의 실행 근거는 아니다.
이후 새 Pawn/BP GameMode/서버 정의·보상/실습 안내를 실제 맵에 연결한 뒤 실행한다.

정상 UHT/Editor 타깃9작업(신규/변경 CPP와 생성 코드·lib/DLL 링크/metadata)·종료0·
선택 Source/Plugins 입력1,381 불변과 모듈 `28A3C386…53786586`을 확인했다. 같은 모듈의
닫힌 사본에서 실제 로드 경로/해시·UE0·native3 Success/사례 오류·경고0,
기동 Error15/발견 이후0이다. 검사에서 쓰는 상태 주입은 권한 근거가 아니다.
증거: `Saved/BuildChecks/ProgressionIntro1Completion-20261102/Verification.proof.json`와
`ba16a90480254ef39d140fc9a006ef7a/{Result,NativeReport}.json`·명령/해시/최소 로그.

자기 runtime/DLL/Config/UserDir/캐시와 적용 완료된 중복 작성본5개를 계측 최소
184,929,242 bytes(176.36MiB) 폐기·소유 링크10개 해제·Temp/정리 예외0이다. 중복 raw
로그의 이미 삭제된 용량은 미계측/합계 제외다. 정상 Binaries/Intermediate·공유 DDC와
다른 담당자의 변경은 보존했다. 폐기한 사본의 해시는 당시 증거만 남긴다.

## 맵 작성 확인 범위

실행 근거는 `Saved/BuildChecks/ProgressionIntro1Arena-20261102/Verification.proof.json`에
남겼다. 엔진 전용 두 에디터 프로세스 종료0·각 Error0이며 생성1/동일 프로세스 반복0/
새 프로세스 읽기 전용 재로드0·동일 SHA256
`5D7AC18CD411441D6D96BA4AD4687DA725750D0D4BEC95C808A468246611653B`다.
25개 소유 배우(도형13/시작점4/구역3/표지3/조명2), 좌표/태그/충돌 설정과
preview mode를 대조했다. 이후 원본 목표 파일이 여전히 없는지 재조회해 overwrite=false로
53,114 bytes 맵 하나만 설치하고 같은 해시를 확인했다. 기존 훈련 맵·훈련 작성기·기본 게임/
backend 설정의 해시를 보존했다. Project_RPG 모듈 로드·게임 Source/공유 빌드 변경0이다.

첫 검사는 색상 생성자의 채널 순서 차이로 저장 전에 실패했다. 명명 인자로 수정했고,
첫 Zen 캐시의 지연된 run.lock 해제 후 남은 자기 폴더를 회수했다. 두 번째는 스크립트 실행
전 TurnkeySupport 초기화의300초 초과로 종료·자동 정리됐다. 두 실패를 최종 성공으로
덮지 않고 `Attempt1`/`Attempt2`에 보존한다. 최종은600초 한도로 처음 저장/재로드를 완료한
별도 실행이다. 최종 실행 임시2,389,712 bytes와 초기 회수268,567,110 bytes/시간 초과
54,944 bytes를 합한 **계측 최소271,011,766 bytes(258.46MiB)**를 폐기했다. 첫 삭제 시도 중
이미 지워진 일부 용량은 미계측이며 이 합계에 넣지 않았다. 최종 Temp/정리 예외0·타 프로세스
종료0·최종 맵/작은 명령/해시/최소 로그만 유지한다. 원본 Binaries/Intermediate·공유 DDC는
삭제하지 않았다.

GPU 화면·물리 이동/공격·네트워크·목표 성공·정산 검사는 이 맵 작성 검증에 포함되지 않는다.
