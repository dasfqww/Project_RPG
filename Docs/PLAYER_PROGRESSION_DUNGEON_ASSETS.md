# 첫 성장 구간의 로비 선택용 던전 정의

작성일: 2026-10-29  
상태: **에셋 5개 작성·저장·새 프로세스 재로드 완료. 로비 공개와 실제 콘텐츠 실행은 후속.**

[콘텐츠 연결 문서](PLAYER_PROGRESSION_CONTENT_INTEGRATION.md)의 5개 ID/6개 난이도 조합을 기존 `URPGDungeonDefinition`으로 작성했다. 경험치 전용 보상 코드는 다른 담당자의 예약이 있어 수정하지 않았으며, 새 정의는 별도 `Progression` 폴더에만 생성했다.

## 작성한 에셋

| 콘텐츠 ID | 표시 이름 | 허용 난이도 | 에셋 |
|---|---|---|---|
| `Progression.Training` | 기본 훈련 | Normal | [DA_Dungeon_Training](../Content/System/Dungeons/Progression/DA_Dungeon_Training.uasset) |
| `Progression.Intro1` | 입문 1: 접근 | Normal | [DA_Dungeon_Intro1](../Content/System/Dungeons/Progression/DA_Dungeon_Intro1.uasset) |
| `Progression.Intro2` | 입문 2: 공격 기회 | Normal | [DA_Dungeon_Intro2](../Content/System/Dungeons/Progression/DA_Dungeon_Intro2.uasset) |
| `Progression.Intro3` | 입문 3: 무력화 | Normal | [DA_Dungeon_Intro3](../Content/System/Dungeons/Progression/DA_Dungeon_Intro3.uasset) |
| `Progression.FirstBoss` | 첫 보스 | Normal, Hard | [DA_Dungeon_FirstBoss](../Content/System/Dungeons/Progression/DA_Dungeon_FirstBoss.uasset) |

기본 난이도는 모두 Normal이다. 표시 이름은 첫 구간의 학습 목표에 맞춘 현재 작성값이며 최종 보스 이름을 확정하지 않는다. 이 정의에는 경험치·SP·해금 플래그·맵·보상 권한을 넣지 않았다. 해당 규칙의 원본은 기존 backend 성장/할당과 UE 서버 정의다.

## 작성 방식과 보존 범위

[CreateProgressionDungeonDefinitionAssets.py](../Scripts/Editor/CreateProgressionDungeonDefinitionAssets.py)는 기본 실행에서 읽기만 수행한다. `-RPGCreateProgressionDungeonDefinitions`를 전달하면 누락된 다섯 대상만 생성한다. 기존 대상이 있으면 클래스·ID·이름·난이도/기본값과 데이터 검증을 먼저 확인하고 저장하지 않는다. 다른 내용의 대상이나 기존 던전 폴더에서 같은 ID를 가진 에셋을 발견하면 보존을 위해 중단한다. 모든 기존 대상을 확인한 뒤 생성하며, 임의의 기존 설정을 갱신하는 마이그레이션 도구로 사용하지 않는다.

기존 Asset Manager의 `/Game/System/Dungeons` 스캔 범위에 포함되지만, 로비는 `DungeonDefinitions`에 명시된 참조만 읽는다. 이번에는 `Config/DefaultGame.ini`와 목록을 바꾸지 않았으므로 기존 로비 선택지는 그대로다. 서버/보상 정의·맵·GameMode·backend 카탈로그도 수정하지 않았다. 정의 생성만으로 잠긴 콘텐츠가 해금되거나 미완성 던전에 입장할 수 있게 되지 않는다.

## 확인한 결과

근거는 [ImplementationResult.json](../Saved/BuildChecks/ProgressionDungeonAssets-20261029/ImplementationResult.json), 같은 폴더의 `Apply.log`/`Applied.json`, `Reload.log`/`Reload.json`이다.

- 기존 정상 DLL `AA6C9CB0…0127810`으로 독립 Cmd/UserDir에서 다섯 에셋을 저장했다. 작성 프로세스 종료 코드는 0이다.
- 다섯 정의의 네이티브 데이터 검증은 모두 Valid이며 검증 오류·경고는 없다.
- 새 프로세스의 읽기 전용 재로드도 종료 0, 저장 0개·누락 0개다. 필드·다섯 파일 해시가 작성 결과와 같고 대상의 dirty 상태가 남지 않았다.
- 선택한 보존 대상 20개는 전후 해시가 같다. 기존 던전/서버 에셋·보스 맵/GameMode·일반/고난도 보상·HUD·Config/backend 기본 설정·관련 기존 소스/작성 도구·공유 DLL을 포함한다. 전체 저장소의 다른 변경까지 없었다는 뜻은 아니다.
- Python 문법 확인을 통과했다. 새 C++/UBT 빌드·공유 GUI/MCP 조작·PIE/실제 로비·HTTP/DB·전투 검사는 실행하지 않았다.

각 실행의 에셋 보고서는 독립 UserDir의 Saved 아래에 생성되며, 위 근거 폴더에 `Applied.json`/`Reload.json` 사본을 두었다. 기존 에디터와 다른 담당자의 프로세스는 종료하거나 재시작하지 않았다.

## 다음 연결

훈련 목표의 서버 판정과 실제 맵을 연결한 뒤 해당 ID의 서버/보상 정의를 작성하고 GameMode 참조를 추가한다. 로비 목록과 backend 카탈로그 공개는 이 연결이 준비된 콘텐츠부터 함께 진행한다. 아직 과제 목표가 없는 A1~A3와 첫 보스를 목록에 미리 공개하지 않는다.

다른 담당자가 구현한 [경험치 전용 보상 작성 경계](DUNGEON_REWARD_INVENTORY_FIRST.md#경험치-전용-완료의-작성-경계-2026-10-29)는 그 담당자의 분리 검증 결과다. 현재 공유 DLL의 새 보상 경계 반영이나 실제 훈련 완료 → 정산 → 경험치 지급은 이번 에셋 재로드의 검증 범위에 포함되지 않는다. 다음 착수 전에도 최신 조율 표의 담당 경로와 고정 입력을 확인한다.
