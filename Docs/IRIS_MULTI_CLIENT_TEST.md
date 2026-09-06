# Iris 전용 서버–다중 클라이언트 테스트

프로젝트의 `GameNetDriver`는 Iris를 우선 복제 시스템으로 사용한다. 모든
`RPGGameModeBase` 파생 게임 모드는 `RPGGameStateBase`를 생성하며, 서버가
작성한 다음 상태를 전체 클라이언트에 복제한다.

- 현재 서버의 원격 `NetConnection` 접속자 수
- 테스트에서 기대하는 클라이언트 수
- 서버 `GameNetDriver`의 실제 Iris 활성 여부
- 상태 변경 리비전

설정 파일만 검사하는 방식이 아니라, 각 클라이언트가 복제된 스냅샷을
수신하고 자기 `GameNetDriver`도 Iris인지 확인해야 테스트가 통과한다.

## 자동 스모크 테스트

프로젝트 루트의 PowerShell에서 실행한다.

```powershell
powershell -ExecutionPolicy Bypass -File .\Scripts\Network\Run-IrisMultiClientTest.ps1
```

기본 동작은 다음과 같다.

1. UE 5.8 Development Editor 타깃을 빌드한다.
2. `/Game/Maps/testmap`을 포트 `17777`의 별도 전용 서버 프로세스로 연다.
3. 헤드리스 클라이언트 두 개를 서버에 직접 연결한다.
4. 서버와 두 클라이언트가 모두 `Iris=1`, `Connected=2`를 관측했는지 판정한다.
5. 성공 또는 실패 후 테스트가 시작한 프로세스만 종료한다.

헤드리스 프로세스는 기본 60 FPS로 제한해 로컬 CPU 포화와 CharacterMovement
SavedMove 과적재를 방지한다. 필요하면 `-MaxFPS 30`처럼 조정할 수 있다.

성공 로그는 다음 디렉터리에 남는다.

```text
Saved/Logs/IrisMultiClient/<실행시각>/
```

화면으로 직접 확인하려면 다음처럼 실행한다.

```powershell
powershell -ExecutionPolicy Bypass -File .\Scripts\Network\Run-IrisMultiClientTest.ps1 -VisibleClients -KeepRunning
```

클라이언트 수와 포트도 바꿀 수 있다.

```powershell
powershell -ExecutionPolicy Bypass -File .\Scripts\Network\Run-IrisMultiClientTest.ps1 -ClientCount 4 -Port 17778
```

이미 빌드한 바이너리를 재사용하려면 `-SkipBuild`를 붙인다.

기본 `Editor` 런타임은 Cook 없이 가장 빠르게 검증한다. 프로젝트를 연 Unreal
Editor가 있으면 DLL 링크가 잠기므로 모두 닫은 뒤 실행해야 한다. 에디터를
유지한 상태에서 이미 빌드하고 Cook한 게임 바이너리를 사용하려면 다음처럼
실행한다.

```powershell
powershell -ExecutionPolicy Bypass -File .\Scripts\Network\Run-IrisMultiClientTest.ps1 -Runtime Game -SkipBuild
```

Epic Launcher 배포형 엔진은 UBT Server 타깃 빌드를 지원하지 않는다. 소스
빌드 엔진을 사용 중일 때만 `-BuildServerTarget`을 추가해
`Project_RPGServer` 타깃까지 함께 검증한다. `Editor -server`와 `Game -server`
테스트도 각각 독립 서버 프로세스와 외부 클라이언트 프로세스로 실행된다.

## 로컬 인증 경계

테스트 서버는 `-RPGNetTestMode`로 실행되므로 백엔드 Join Ticket을 요구하지
않는다. 이 우회는 `UE_BUILD_SHIPPING`에서는 컴파일상 항상 비활성화된다.
일반 전용 서버 실행에서는 기존 `PreLoginAsync` Join Ticket 검증이 그대로
적용된다.

`RPG_NETTEST SERVER_READY`와 모든 `RPG_NETTEST CLIENT_READY` 마커가 함께
기록되어야 다중접속 기반이 정상으로 판정된다.
