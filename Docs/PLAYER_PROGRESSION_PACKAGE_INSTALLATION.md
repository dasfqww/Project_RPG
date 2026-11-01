# 첫 성장 구간 패키지의 원본 설치

2026-11-01 상태: **재료·입문 보상6파일과 장비8파일을 원본에 설치했다.** 원본14개와 작성 패키지의 바이트/체크섬이 같고 설치 후 Plan 누락은0이다. 이번 작업에서 새 native 재로드·실제 지급/전투·서비스 배포는 하지 않았다.

## 적용한 파일

| 경로 | 파일 | 개수 |
|---|---|---:|
| `Content/Blueprints/GameData/Item/Progression/` | `DA_Item_ProgressionOre`, `DA_Item_ProgressionToken` | 2 |
| 위와 같음 | `DA_Item_Progression_{StarterGreatSword,StarterChest,StarterGloves,EnduranceGreatSword,CirculationGreatSword,TargetChest,TargetGloves}` | 7 |
| `Content/Blueprints/GameData/DungeonReward/Progression/` | `DA_DungeonReward_Intro{1,2,3}` | 3 |
| `Config/Tags/` | `ProgressionMaterials.ini`, `ProgressionEquipment.ini` | 2 |

에셋 파일은 `.uasset`이며 총 설치 용량은 **35,749 bytes**다. 기존 설치기 두 개의 사전 Plan을 모두 통과한 뒤 `-Apply`로 누락 파일만 복사했다. 신규14/기존 대상 보존0/덮어쓰기0이고, 설치 후 두 Plan은 각각 누락0/복사0이다. 최종14개 해시와 수정시각도 확인했다.

재료는 정확한 Ore/Token ID·Version1·최대 스택999, 입문 보상은 광석2개씩이다. 장비는 작성한 시작3종/목표4종의 Version1·슬롯9/1/4를 사용한다. 실제 등록/속성의 작성 당시 native 검증은 기존 [재료 패키지](PLAYER_PROGRESSION_MATERIAL_ASSETS.md)와 [장비 계약12절](PLAYER_PROGRESSION_EQUIPMENT_TRANSACTIONS.md#12-2026-10-31-시작목표-장비-정의-패키지)의 과거 근거다. 이번 파일 설치 확인을 현재 모듈의 새 native 검증으로 해석하지 않는다.

작성 패키지16파일(Manifest 포함)과 기존 대검 template의 해시는 설치 전후 같다. Manifest의 `installedInOriginalProject=false` 등은 **패키지 작성 당시 기록**으로 보존했으며 현재 설치 여부는 이 문서와 새 설치 보고서를 따른다. 패키지를 재작성하거나 원본 에셋을 새로 저장하지 않았다.

## 다른 작업과의 경계

착수 때 HUD 담당은 명시한 모듈/GA 에셋과 overlay 사본을 사용하고 원본 장기 읽기는 없다고 기록했다. 설치14개는 그 에셋과 겹치지 않는다. UE 보상 전달7파일은 `progression-reward-ue-20261101` 담당이 구현 중이므로 별도로 구현하거나 수정하지 않았다.

native 재로드를 준비하던 중 이 담당이 정상 DLL/공유 빌드를 새로 예약했다. 해당 사본/실행 범위를 철회하고 원본 설치·체크섬 확인만 진행했다. Binaries/Plugins/전체 Config·Content/Source 사본, Cmd/PIE·공유 빌드·타 프로세스 종료를 실행하지 않았다. 미실행 snapshot 초안2개는 폐기하고 실제 [설치 명령](../Saved/BuildChecks/ProgressionPackageApply-20261101/InstallOnly.ps1)만 유지한다.

기존 [장비 계약](PLAYER_PROGRESSION_EQUIPMENT_TRANSACTIONS.md)의 미설치 문구는 앞선 시점의 기록이며, 그 파일은 다른 담당이 후속 작성 중이어서 이번에 수정하지 않았다. 최신 원본 설치 상태는 이 문서를 우선 참조한다. 조율표에는 본인 결과만 추가하고 다른 담당의 예약을 해제하지 않았다.

## 남은 연결

1. UE 보상 담당의 정상 출력/고정 입력 사용 종료·명시적 해제를 확인한 뒤 새 모듈로 등록 ID/정의 속성/태그·보상 참조의 native 재로드를 별도 예약한다. 설치가 같은 바이트라는 이유만으로 현재 모듈의 성공으로 선언하지 않는다.
2. 시작3종 지급 후보는 여전히 원본 미적용이다. 원본 backend 새 문맥/해시와 한 자루를 가정한 UE 검사 소비자의 인계 범위를 확인한 뒤 적용한다. 현재 신규 캐릭터 생성은 기존 대검1개 지급을 유지한다.
3. 원본 에셋 배치는 GameMode의 입문 보상/서버 참조·실제 재료 지급, 개인별 보스 추첨, UE 성장 명령/UI/GAS 연결을 대신하지 않는다. 이번에 backend/기존 DB/서비스/클래스·캐릭터 데이터를 변경하지 않았다.

근거는 [ImplementationResult.json](../Saved/BuildChecks/ProgressionPackageApply-20261101/ImplementationResult.json), 설치 전/적용/설치 후의 두 보고서와 `InstalledFiles.json`·`SourceHashes.json`이다. 대형 임시 검증 환경은 생성하지 않았고 실제 최종14파일은 유지한다. 미사용 초안의 정리 용량/예외는 `DraftCleanup.json`에 기록했다.
