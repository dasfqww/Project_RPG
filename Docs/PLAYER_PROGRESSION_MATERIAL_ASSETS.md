# 첫 성장 구간의 재료·입문 보상 적용 패키지

작성일: 2026-10-30  
상태: **아이템2개·보상3개/태그 INI의 원본6파일 설치와 체크섬 확인 완료. 현재 모듈의 새 native 재로드·실제 입문 지급은 후속이다.** [최신 설치/인계 범위](PLAYER_PROGRESSION_PACKAGE_INSTALLATION.md)를 따른다. 기존 작성/독립 재로드·설치기 점검은 당시 근거이며 backend 강화/제작·분해는 별도 구현을 완료했다.

2026-10-31의 보류 기록: 당시 platform-player-security와 후속 HUD/런처 담당의 Content/Config 입력 때문에 원본 설치를 보류했다. 이 경계는 2026-11-01 최신 조율에서 명시적 에셋 쓰기/닫힌 사본으로 분리할 수 있어 설치6개를 완료했다. 아래 훈련 진행 중/재개·미설치 기록은 작성 당시 사유다. 현재 정상 DLL/공유 빌드는 UE 보상 담당이 예약했으므로 새 native 재로드는 그 인계 후 진행한다. 장비 기능의 최신 연결은 [실행 계약](PLAYER_PROGRESSION_EQUIPMENT_TRANSACTIONS.md)의 후속 절과 새 설치 문서를 구분해 참조한다.

[첫 구간 수치안](PLAYER_PROGRESSION_FIRST_SLICE_V01.md#51-개인별-클리어-보상)의 입문 광석 2개와 이후 증표 제작에 사용할 식별자를 실제 데이터 에셋으로 작성했다. 최종 결과물은 [ContentAuthoring/ProgressionMaterials](../ContentAuthoring/ProgressionMaterials/Manifest.json)에 보관한다. 다섯 에셋·태그 INI·매니페스트의 총 7개 파일/22,304 bytes는 최종 산출물이며 임시 검증 파일이 아니다.

## 데이터와 적용 범위

| 아이템 에셋 이름 | 안정 ID | 작성값 |
|---|---|---|
| `DA_Item_ProgressionOre` | `RPGItemDefinition:GameItem.Material.Progression.Ore` | 성장 광석, 정의 버전 1 |
| `DA_Item_ProgressionToken` | `RPGItemDefinition:GameItem.Material.Progression.Token` | 제작 증표, 정의 버전 1 |

두 아이템의 패키지 경로는 `/Game/Blueprints/GameData/Item/Progression/<에셋 이름>`이다. 기존 `URPGItemDefinition`/Craft 분류와 1×1 칸을 사용하며 장비·소비 효과/능력치 fragment는 없다. 최대 스택 999는 이번 작성값이다. 현재 인벤토리 용량이나 강화 비용을 바꾸는 값이 아니며 변경 시 저장된 정의 버전·스택 호환성을 확인해야 한다. 전용 아이콘·월드 픽업 외형은 아직 작성하지 않았다.

| 보상 에셋 이름 | 사용 예정 ID | 실제 내용 |
|---|---|---|
| `DA_DungeonReward_Intro1` | `Progression.Intro1` | 광석 2개·CharacterBound |
| `DA_DungeonReward_Intro2` | `Progression.Intro2` | 광석 2개·CharacterBound |
| `DA_DungeonReward_Intro3` | `Progression.Intro3` | 광석 2개·CharacterBound |

보상 경로는 `/Game/Blueprints/GameData/DungeonReward/Progression/<에셋 이름>`이다. 세 보상은 모두 `FirstSlice.v1`, 재화 0·증표 0·장비 추첨 없음·추가 태그/능력치/내구도 없음이다. 귀속은 **보상 엔트리**의 `CharacterBound`이며 아이템 정의 자체가 모든 미래 생성 경로의 귀속을 강제하는 것은 아니다. 다른 생성/제작 경로도 기존 아이템 서비스의 소유·귀속 정책을 따라야 한다.

경험치/SP를 아이템 또는 지갑 재화로 작성하지 않았다. 실제 세션이 올바른 입문 콘텐츠 ID/버전으로 완료·정산될 때의 XP 100·진행 플래그/해금은 기존 backend 성장 규칙이 담당한다. 에디터 메타데이터의 `RPGIntendedDungeonId`는 사용 예정 대조용이며 권한이나 서버 카탈로그 매핑이 아니다. 증표 정의는 이후 제작/보스 보상의 준비물로만 존재하며 이번 세 보상에서는 지급하지 않는다. 보스의 개인별 장비 추첨·강화·이전·제작·분해는 구현하지 않았다.

## 원본 Content에 설치하지 않은 이유

기존 `UDataManager::RefreshItemCache()`는 `/Game` 전체의 네이티브 아이템 정의를 로드한다. 진행 중인 `training-playtest-20261030`의 원본 Content/태그 입력을 유지하기 위해 독립 RuntimeProject의 **자체 Content·Config**에서 작성했다. Plugins만 링크로 참조했고 원본 Content에는 링크나 쓰기를 만들지 않았다. 원본 Config/Tags에도 새 파일을 추가하지 않았다.

이 패키지는 UE가 원본 `/Game`에서 자동 검색하는 위치 밖에 있다. 현재 게임 카탈로그에 추가됐거나 가방에 표시·지급되는 상태로 보고하지 않는다. 원본 프로젝트의 기존 Primary Asset 스캔 디렉터리는 설치 후의 아이템 경로를 이미 포함하므로 공용 스캔 설정을 새로 작성할 필요는 없다.

## 작성기와 확인 결과

[CreateProgressionMaterialAssets.py](../Scripts/Editor/CreateProgressionMaterialAssets.py)는 기본 읽기 전용이다. `-RPGCreateProgressionMaterialAssets`는 누락된 다섯 대상만 만들고, `-RPGProgressionMaterialAssetsReload`는 저장 없이 확인한다. `RPG_PROGRESSION_MATERIAL_REPORT`에 결과 JSON의 절대 경로를 지정한다. 대상 프로젝트가 먼저 [태그 INI](../ContentAuthoring/ProgressionMaterials/Config/Tags/ProgressionMaterials.ini)를 로드해야 한다.

기존 대상의 마커·태그·버전·표시/스택·보상 내용을 첫 수정 전에 확인하고 다른 설정/dirty 대상은 보존하며 실패한다. 대상 경로가 Content 밖으로 나가는 링크도 거절한다. 이후 재실행에서 기존 에셋을 덮어쓰거나 다시 저장하지 않는다. 실제 ID의 공개 조회는 Asset Manager 등록에 의존하므로 새 에셋의 저장 전에는 비어 있을 수 있다. 새 프로세스 재로드에서는 두 ID가 실제 등록됐는지 엄격히 확인한다.

[ImplementationResult.json](../Saved/BuildChecks/ProgressionMaterialPackage-20261030/ImplementationResult.json)과 Apply/Repeat/Reload JSON·필수 로그가 근거다.

- 기본 게임 DLL `AA6C9CB0…0127810`의 자체 사본으로 작성했으며 새 C++/공유 UHT·UBT/Intermediate·분리 링크를 실행하지 않았다. 자체 Config만 엔진 기본 GameInstance/맵을 사용했다.
- 작성/반복/독립 재로드 **종료 0·저장 5/0/0**, 다섯 파일의 해시·보고된 설정이 같다. 다섯 에셋 모두 `Valid`/검증 오류·경고 없음·dirty=false이며 두 안정 ID의 실제 등록을 확인했다.
- 최종 세 Cmd 로그 Error/Fatal 0, 기존 엔진 경고 4줄씩은 요약 재출력을 포함해 따로 보존했다. 원본 공통 코드·설정·기존 아이템/보상·DLL 20개의 해시가 같고 원본의 새 대상/태그 INI는 모두 부재다.
- 최초 별도 태그 파일의 배열 연산자 표기 때문에 저장 전 실패했다. 독립 태그 목록의 일반 배열 표기로 고친 뒤, 새 에셋의 저장 전 ID 조회를 등록 완료로 취급하던 두 번째 실패도 발견했다. 저장 전/새 프로세스 등록 확인을 구분해 보완했으며 두 실패는 최종 성공과 별도 기록이다. 원본이나 최종 에셋을 덮어쓰지 않았다.

세 시도의 Temp/DLL·RuntimeProject/UserDir/캐시·중복 Console 로그 48,871,071 bytes를 finally에서 폐기했고 상세 로그도 1,285,817 bytes 축소했다. **총 50,156,888 bytes(47.83MiB) 정리·잔여 Temp/예외 없음**이며 작은 결과·명령·해시·필수 로그와 최종 적용 패키지만 남긴다. 삭제한 DLL 사본의 해시는 당시 증거이고 재실행에는 새 사본이 필요하다. 사용자 에디터/다른 담당의 프로세스·실행 입력은 수정하거나 종료하지 않았다.

## 설치와 다음 연결

1. 최신 조율 표에서 훈련 검증 담당의 입력 사용 종료/인계와 원본 Content/Config의 수정 범위를 확인한다.
2. `Manifest.json`에 적힌 여섯 파일(에셋 5개·태그 INI)의 해시를 확인한다. 같은 상대 경로의 원본 대상이 있으면 같을 때만 유지하고 다르면 덮어쓰지 않는다.
3. 아래 설치기로 패키지의 `Content/`와 `Config/Tags/` 상대 경로를 원본 프로젝트에 설치한다. 태그를 로드한 새 프로세스에서 작성기의 읽기 전용 재로드를 실행해 실제 ID/참조를 확인한다. 원본 설치는 아직 실행하지 않았다.
4. 입문 목표·맵/서버 정의가 준비되면 Normal의 해당 보상 참조를 연결한다. 기존 가방 우선/초과 우편·캐릭터 귀속·정산 재시도 계약과 실제 XP/광석 지급을 확인한 뒤 공개한다.

입문 콘텐츠/보상·아이템 API 또는 카탈로그의 중복 구현은 추가하지 않는다. 실제 훈련 E2E와 HUD는 현재 다른 담당의 범위다.

## 2026-10-30 설치기와 적용 계획 점검

[InstallProgressionMaterialPackage.ps1](../Scripts/Editor/InstallProgressionMaterialPackage.ps1)를 추가했다. 기본 실행은 계획만 출력하며 원본에서 **누락 6개·복사 0개**를 확인했다. `-Apply`가 있을 때만 누락된 파일을 복사한다. `-TargetProjectRoot`로 독립 대상 프로젝트를 지정할 수 있고 `-PackageRoot`/`-ReportPath`로 입력 패키지와 Saved 결과 경로를 지정한다.

```powershell
# 원본 적용 계획만 확인
powershell -NoProfile -File Scripts/Editor/InstallProgressionMaterialPackage.ps1

# 최신 조율 표의 원본 Content/Config 읽기 종료·쓰기 범위 등록 후 실행
powershell -NoProfile -File Scripts/Editor/InstallProgressionMaterialPackage.ps1 -Apply
```

설치기는 조율 표나 실행 중 게임을 자동 관리하지 않는다. 호출자가 입력 사용 종료/예약을 확인해야 한다. 이번에는 설치 범위를 등록·재조회하는 사이 `training-playtest-20261030`이 정상 모듈 기반 E2E를 재개했으므로 **원본 쓰기 예약을 취소하고 독립 폴더에만 실제 설치**했다. 원본 Content/Config·훈련/HUD·공유 빌드·DLL/GUI/기존 파일은 수정하지 않았으며 UE/백엔드를 실행하지 않았다.

안전한 설치를 위해 매니페스트 버전과 정확한 여섯 상대 경로·소스 해시·모든 기존 대상을 첫 복사 전에 확인한다. 같은 기존 파일은 보존하고 다른 파일, 변조된 패키지, 루트 밖 경로·junction/symlink 경유는 거절한다. 파일별 복사는 덮어쓰기를 허용하지 않으며 복사 뒤에도 해시를 확인한다. 전체 사전 점검 뒤 외부 I/O 오류가 나면 이미 추가한 파일을 보고하고 자동 삭제/롤백하지 않는다. 충돌 원인을 해결한 뒤 동일 파일을 보존하는 재실행 경로를 사용한다.

[Result.json](../Saved/BuildChecks/ProgressionMaterialInstall-20261030/Result.json)이 설치기 점검의 근거다. 원본/독립 계획, 독립 실제 설치 6개, 반복 설치 0개/기존 6개 타임스탬프 보존, 마지막 대상의 기존 다른 파일 보존, 소스 체크섬 거절, junction 대상 보존, 허용 목록 밖 매니페스트 거절의 **8개 그룹**을 확인했다. 거절 4건은 예상 종료 1·복사 0이며 정상 사례는 종료 0이다. 이는 파일 설치 점검이며 게임/보상 정산 검증이 아니다.

처음에는 설치기의 성공 종료 코드가 명시되지 않아 호출자가 null을 관찰했다. 원본 계획은 성공했지만 첫 호스트 점검은 실패로 보존했고, 성공 시 종료 0을 명시한 뒤 최종 8개 그룹을 통과했다. 초기/최종 fixture·중복 출력 **118,790 bytes(약 116KiB)**를 finally에서 정리했으며 Temp 잔여/정리 예외는 없다. 최종 패키지 7개 파일의 해시는 같고 원본 설치 대상 6개는 여전히 부재다.

다음은 훈련 E2E의 Content/Config 읽기 사용 종료·인계 확인 → 원본 설치 범위 재등록 → `-Apply`/새 프로세스 읽기 전용 재로드다. 패키지 매니페스트의 `installedInOriginalProject=false`는 작성 당시 기록이며 실제 설치 결과는 설치기의 Saved 결과와 원본 해시로 별도 확인한다.
