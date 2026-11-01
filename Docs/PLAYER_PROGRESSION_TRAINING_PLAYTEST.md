# 훈련 입력부터 서버 정산까지의 실행 검사

`Run-TrainingPlaytest.ps1`는 새 대검 캐릭터로 실제 훈련 맵에 입장해 기본 공격, 회피, 어썰트, 완충 차지를 수행하는 1인 통합 검사다. [개인 목표](PLAYER_PROGRESSION_TRAINING.md), [차지 해제](PLAYER_PROGRESSION_TRAINING_CHARGE.md), [파티 완료 집계](PLAYER_PROGRESSION_TRAINING_COMPLETION.md)의 연결을 확인한다.

현재 상태: **2026-10-30 개발 인증/Memory/HTTP 티켓/Iris의 실제 1인 입력 → 네 목표 복제 → 서버 정산 → 경험치 저장 통과.** 2~4인 파티·Steam·PostgreSQL·물리 입력/GPU·로비 공개는 별도다.

## 실행 경계

- 명시적 `-RPGTrainingPlaytest -nosteam`을 전달한 Development 테스트 프로세스에만 일반 C++ ticker가 설치된다. 반영 타입이나 저장 형식은 추가하지 않는다.
- 테스트는 개발 인증 후 기존 캐릭터 선택/세션 재개/티켓 입장을 사용한다. 로비와 훈련 맵의 실제 PlayerController/Pawn/GAS를 사용하며 Iris를 요구한다.
- 기본 공격/회피는 기존 ASC 입력, 스킬은 실제 슬롯을 SkillTag로 찾아 Controller와 동일한 공개 퀵슬롯 BeginUse/EndUse를 호출한다. 목표 비트 설정, Ability 강제 종료, 몬스터/피격 결과 주입, 완료 보고/보상 API 직접 호출은 하지 않는다.
- 개인 목표의 서버 값과 소유자 복제 값이 모두 `15`에 도달하고, 실제 GameMode가 세션을 `Cleared`로 정산해야 성공한다.
- 처음 경험치 0/레벨 1/SP 0에서 경험치 100/레벨 2/SP 3, 회전 베기와 고유 능력 해금을 확인한다. 반복 조회의 중복 지급 방지와 같은 계정의 다른 캐릭터 불변도 검사한다.
- 운영 Steam/PostgreSQL 설정이나 공개 던전 목록은 바꾸지 않는다. 해당 실행의 Memory 백엔드 환경 변수에서만 훈련 ID와 실제 맵을 연결한다.

## 실행

현재 소스와 일관된 테스트용 게임 모듈을 먼저 준비한다. 훈련 GameMode의 실제 BP 부모 연결과 `RPGTrainingPlaytestProbe.cpp`가 포함돼야 한다. 다른 담당이 수정 중인 소스나 공유 빌드 입력을 예약 없이 빌드하지 않는다.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/Tests/Run-TrainingPlaytest.ps1 `
  -RuntimeProjectRoot <분리된 RuntimeProject 절대경로>
```

기본 포트는 백엔드 TCP 3074와 UE UDP 18894다. 점유 중이면 시작을 거절한다. 서버와 클라이언트의 실제 로드된 모듈 경로/SHA256을 확인한다.

백엔드 소스/빌드/Memory 저장소와 두 UE 프로세스의 UserDir는 실행별로 분리한다. 종료와 실패 시 자기 프로세스만 회수한 뒤 `finally`에서 임시 입력/빌드/원본 실행 로그/UserDir/캐시를 폐기한다. 작은 JSON 결과·백엔드 입력 해시·자격 증명을 제외한 핵심 로그만 남긴다. 모듈을 만든 호출자는 별도로 자기 DLL/PDB/OBJ와 RuntimeProject를 폐기한다.

## 2026-10-30 기본 공격·회피 태그 보완과 1인 전체 흐름 통과

공통 빌드에는 새 HUD 표현 작업의 예약이 생겨, 이미 설치·검증된 정상 DLL `4BCAB2FB…7B764E7`을 자기 실행 프로젝트로 복사했다. 활성 Source/Policy/HUD·UHT/UBT/Intermediate를 사용하거나 원본 DLL을 교체하지 않았다. 실제 서버와 소유 클라이언트가 이 사본을 로드한 경로/해시를 기록했다. 진행 중인 HUD 표현 소스의 결과를 검증한 것은 아니다.

수정 전 실제 훈련 실행 `a3caecd6f9fe475fa0b4caf7456b8e04`는 인증 입장·직업/슬롯 복원과 활성 목표 0까지 도달했지만, 기본 공격 입력 후 완료가 기록되지 않았다. 새 읽기 전용 작성기로 두 CDO를 확인한 결과는 다음과 같다.

| 에셋 | 기존 분류 | 보완 |
|---|---|---|
| `DefaultAttack/GA_Player_DefaultAttack` | `Player.Ability.Attack.Light` | 기존 범용 태그를 유지하고 `Player.Ability.Attack.Light.GreatSword` 추가 |
| `GA_Player_Roll` | `Player.Roll` | 현재 네이티브 회피 분류 `Player.Ability.Roll`로 교체 |

[ConfigureGreatSwordTrainingAbilityTags.py](../Scripts/Editor/ConfigureGreatSwordTrainingAbilityTags.py)는 기본 읽기 전용이며 명시적 `-RPGConfigureTrainingAbilityTags`에서만 저장한다. 부모 타입·dirty/PIE·계획 중 원본 해시·관련 기본값을 검사하고 예외 시 자기 저장만 해시 대조 후 복구한다. 실제 적용 전 GUI29992의 PIE=false/열린 에셋 없음/두 대상 dirty=false를 읽기만 확인했다. 기본 공격의 몽타주/구간·초기 콤보·초기화 시간, 입력·활성 조건/소유 태그·취소/차단 태그와 네트워크 기본값 등 기록한 속성은 그대로다. 그래프나 서버 완료 조건을 바꾸지 않았다. Config/태그 목록·캐릭터 저장 형식은 수정할 필요가 없었다.

읽기 전용 감사/적용/새 프로세스 반복 적용 모두 종료 0, 저장 **0/2/0**이다. 반복 로드의 canonical 태그·파일 해시가 유지됐다. `NormalizeActionTags-Tags.json`/`RepeatActionTags-Tags.json`에 전후 태그·관련 속성·해시를 보존했다. 래퍼의 첫 재적용은 기존 MCP helper와 StrictMode의 선택적 속성 처리 문제로 쓰기 전에 실패했고, 읽기 전용 wrapper 범위만 보완해 통과했다. 이 실패는 `RepeatActionTags-PreflightFailure.json`으로 구분했다.

최종 실행은 [Verification.proof.json](../Saved/Tests/TrainingPlaytest/68ed2a36ccc2485d94c4f80f7223df44/Verification.proof.json)이다.

- 실제 `L_ProgressionTraining`에 HTTP 티켓으로 입장한 새 레벨 1 캐릭터가 기존 ASC 입력/퀵슬롯 BeginUse/EndUse로 네 조작을 수행했다. 목표 비트·Ability 종료·완료/정산을 강제로 호출하지 않았다.
- 서버와 소유 클라이언트 모두 목표 비트가 **0→1→3→7→15**로 변했다. 기본 공격·회피·돌진의 정상 종료와 완충 차지의 해제/정상 종료가 기존 서버 관찰 경로에서 인정됐다.
- 실제 훈련 GameMode가 기존 경로로 세션을 **Cleared** 처리했다. backend 조회에서 **경험치 0→100, 레벨 1→2, SP 0→3, 회전 베기와 투지 해방 해금**을 확인했다.
- 완료 뒤 성장 기록을 3회 더 읽어도 값/버전이 변하지 않았고, 같은 계정의 다른 캐릭터는 전후 동일했다. 재접속이나 중복 완료 POST를 재현한 검사는 아니다.
- 서버/클라이언트 정상 종료 **0/0**, probe 시작 이후 Error **0/0**, 실행 중 원본/선택 입력 **23개 불변**이다.

소규모 실행 래퍼와 명령·해시는 `Saved/BuildChecks/TrainingPlaytest-20261030/RunInstalled.ps1`, `AfterActionTags-Result.json`, `AfterActionTags-Inputs.json`에 있다. 이번 회차의 자기 DLL 사본·RuntimeProject·백엔드 입력/빌드·UserDir·캐시·원본 로그·임시 복구 사본 **97,185,971 bytes(92.68 MiB)**를 finally에서 폐기했고 잔여/예외는 없다. 기존 원본 DLL과 Content/Plugins 링크 대상은 보존했다. 폐기된 사본의 해시는 당시 증거이며 재실행에는 새 사본이 필요하다.

남은 범위는 **2~4인 전원 완료/미입장/재접속/포기**, Steam·PostgreSQL, 물리 키보드/마우스·GPU 안내 화면, 자연 전투/적중, 로비와 운영 콘텐츠 목록의 훈련 공개다. 첫 훈련은 적중을 요구하지 않는 조작 실습이며 이 결과로 보스 공략 완료를 주장하지 않는다.

## 2026-10-30 초기 검증 기록 — 아래 대기 상태는 후속 통과 이전 기록

초기 도구 검증에서 공개 퀵슬롯 접근 경로·네이티브 태그의 값 형식·실행 로그의 공유 읽기를 보완했다. 분리 컴파일/링크와 Memory 백엔드 빌드가 통과했고, 실제 훈련 서버가 배정 세션을 시작하는 것까지 확인했다. 전용 서버에 클라이언트 전용 `URPGDungeonSessionSubsystem`을 요구했던 probe 조건은 제거했다. 이는 게임의 로비 책임을 서버로 옮기는 변경이 아니다.

초기 두 UE 실행은 로그 읽기와 probe 초기화 오류로 실패했다. 각각 `fae8bbc304fe41a8987f552297f5df20`, `3439f1c386e74e6aa98f021f057c2dd7` 실행 폴더의 실패 proof를 보존했다. 이 결과를 개인 목표 완료나 보상 성공으로 계산하지 않는다. 이후 다른 담당의 공통 NoLink/HUD 에셋 예약을 확인해 재실행을 멈추고 인계를 기다렸다. 이전 실행의 DLL/입력 사본은 모두 폐기했으므로 인계 후 새로 고정한다.

수정된 probe는 후속 공통 NoLink의 실제 컴파일 목록에 포함됐고, 보완 빌드가 성공했다. 최신 probe의 UE 실행은 아직 하지 않았다. 스크립트 3개 구문 검사와 임시 산출물의 경로 이탈/잠긴 파일/링크 대상 보호 검사는 통과했다. 작은 인계 기록은 `Saved/BuildChecks/TrainingPlaytest-20261030/Handoff.json`이다.

추가로 회피 BP의 기존 `Player.Roll` 태그 경고와 직렬화 이름을 확인했다. 에셋에는 `Player.Ability.Roll` 이름이 없고 현재 개인 목표의 회피 판정은 그 태그를 요구한다. 이는 목표 완료를 막을 수 있는 구체적인 불일치이며, 실제 입력 재현 후 정식 에셋 또는 승인된 서버 grant 판정 경로에서 보완해야 한다. 이번 작업에서 회피 에셋/개인 목표 코드는 수정하지 않았다.

공통 검증 자원의 활성 예약이 유지돼 이 지점에서 실행 인계를 남긴다. 자기 검증의 임시 입력/DLL/OBJ/PDB/RuntimeProject/UserDir/캐시 합계 **9,739,778,674 bytes (9.071 GiB)**를 폐기했고 잔여 임시 실행/예외는 없다. 작은 명령/해시/실패 원인 로그만 보존한다. 최종 완료 검사는 공통 예약 해제 재조회 후 새로 빌드한 입력으로 실행하며, 이전 DLL/입력은 재사용할 수 없다.

최종 실행 결과는 이 절에 추가한다. 정상 공유 DLL 반영, 실제 키보드/마우스와 GPU 배치, Steam, PostgreSQL, 2~4인 파티, 재접속/프로세스 재시작은 이 1인 검사 범위 밖이다.
