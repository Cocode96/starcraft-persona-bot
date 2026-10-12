# StarCraft Persona Bot

스타크래프트 리플레이에서 플레이어별 특성을 추출하고, 직접 상대할 수 있는 BWAPI 봇에 반영하는 프로젝트다. 첫 버전은 Lavaa의 저그 건설 명령 시점을 사용하는 연습 봇이다.

사용자가 확정한 목표는 Lavaa와 지인 각각의 평소 플레이를 재현하는 대전 봇이다. Pluto급 실력은 요구사항이 아니다. 구현과 평가는 리플레이에서 관찰되는 개인별 선택과 행동의 유사성을 중심으로 진행한다. 사람별 봇의 재현 정도는 아직 검증하지 않았다.

현재 봇은 채취, 일꾼과 병력 생산, 서플라이 관리, 건설, 적 시작 위치 탐색과 공격을 규칙으로 처리한다. 스포닝 풀, 추가 해처리, 익스트랙터와 히드라리스크 덴의 건설 시점은 리플레이에서 추출한 중앙값을 사용한다. 학습된 신경망과 개인 플레이의 완전한 복제는 아직 구현하지 않았다.

## 현재 검증 범위

- 리플레이 431개를 파싱했고, 바이트가 같은 중복 6개를 제외한 425경기를 분석했다. 파싱 오류는 없었다.
- Lavaa 저그 203경기 중 인간 플레이어 두 명이 서로 다른 팀으로 참여한 179경기로 첫 프로필을 만들었다. UMS 관전용 맵도 포함될 수 있다.
- Python 테스트, Windows x86 DLL 빌드, 실제 생성된 프로필의 C++ 파싱을 검증했다.
- 실제 게임 실행과 봇의 대전 성능은 아직 검증하지 않았다. 확인된 로컬 스타 버전은 리마스터 `1.23.10.13515`이며, BWAPI 공식 실행 환경인 `1.16.1`과 다르다.
- 클라우드 설정과 GitHub Actions 파일을 포함한다. 원격 작업과 Linux 실행 결과는 아직 없다.

## 구성

| 경로 | 역할 |
| --- | --- |
| `persona/` | 리플레이 분석, 닉네임 목록과 프로필 추출 |
| `bot/` | 프로필 설정과 BWAPI 저그 봇 |
| `scripts/` | 의존 도구 준비, DLL 빌드, 클라우드 설정 |
| `tests/` | 데이터 분류, 설정 계약과 DLL 로드 검증 |
| `data/` | 로컬 분석 결과와 개인 프로필, Git 제외 |
| `dist/bwapi-data/` | 빌드한 DLL과 활성 프로필, Git 제외 |

## 로컬 준비

Python 3.11 이상과 Visual Studio 2022의 C++ 데스크톱 개발, CMake 도구가 필요하다. 명령은 저장소 루트에서 실행한다.

```powershell
python scripts/bootstrap.py --bwapi
```

고정 버전 `screp v1.13.7`과 `BWAPI 4.4.0`을 공식 릴리스에서 받아 저장소 내부 `tools/`에 준비한다. 설치된 게임은 수정하지 않는다. screp는 릴리스에 게시된 SHA-256, BWAPI는 이 프로젝트가 기록한 아카이브 SHA-256과 대조한다.

분석 코드는 외부 Python 패키지나 OpenAI API 키 없이 실행한다.

## 리플레이 분석

```powershell
python -m persona.cli scan "$env:USERPROFILE/Documents/StarCraft"
python -m persona.cli players --anchor Lavaa --limit 20
python -m persona.cli profile Lavaa --race Zerg --output data/profiles/Lavaa-Zerg.json
python -m persona.cli export-bot data/profiles/Lavaa-Zerg.json
```

`scan`은 원본을 읽기만 한다. 같은 파일의 복사본은 SHA-256으로 묶고 출처 파일명을 보존한다. 저장 형식이 달라진 동일 경기는 별개로 셀 수 있다. 닉네임은 정확한 문자열로 구분하며 비슷한 닉네임을 자동으로 같은 사람으로 합치지 않는다.

`--opponent-race Protoss`를 `profile` 명령에 추가하면 특정 상대 종족의 경기만 사용할 수 있다. 프로필은 세 종족 모두 추출할 수 있지만 현재 봇 DLL에 내보낼 수 있는 종족은 저그다.

리플레이에는 플레이어의 입력 명령이 기록된다. 따라서 건설 요청이 실패했거나 취소되었을 수 있으며, 이 프로젝트의 건설 시점을 건물 완성 시점으로 해석하면 안 된다. 분석 결과에는 원본 게임 채팅을 포함하지 않는다.

## 봇 빌드와 실행

```powershell
python -m unittest discover -s tests -v
python scripts/build_bot.py
```

`build_bot.py`는 Release/Win32 DLL을 빌드하고 테스트한 뒤 `dist/bwapi-data/AI/PersonaBot.dll`과 `dist/bwapi-data/read/persona.ini`를 준비한다. Visual Studio 솔루션은 `build/StarCraftPersonaBot.sln`이다.

실제 실행에는 별도의 브루드워 1.16.1과 BWAPI 4.4.0 환경이 필요하다. 해당 환경이 준비되면 다음과 같이 설정한다.

1. `dist/bwapi-data/`의 내용을 1.16.1 게임 폴더의 `bwapi-data/`에 복사한다.
2. 해당 설치의 `bwapi-data/bwapi.ini`에서 `[ai]`의 `ai` 값을 `bwapi-data/AI/PersonaBot.dll`로 설정한다.
3. BWAPI가 활성화된 Chaoslauncher에서 실행하고 봇의 종족을 저그로 선택한다.
4. 사람과 대전할 때는 BWAPI가 활성화된 봇 인스턴스와 주입을 끈 사람 인스턴스로 Local PC 방에 입장한다.

봇 화면에 프로필 이름과 표본 수가 표시된다. 설정 파일이 없거나 잘못되면 `Baseline Zerg`라는 기본 규칙으로 동작하고 이유를 출력한다. 저그 외 종족에서는 동작하지 않는다. 기존 리마스터 설치에 이 DLL을 덮어 넣어 실행할 수는 없다.

첫 버전은 지상군 중심의 제한된 봇이다. 초반 압박에 대한 빌드 변경, 고급 컨트롤, 업그레이드, 공중 병력 운영과 완전한 확장 관리는 구현하지 않았다. 추가 해처리는 가능한 가까운 자원 지역을 찾고, 탐색되지 않았거나 적합한 위치가 없으면 본진 안에 지을 수 있다. 명령 시점 중앙값만으로 실제 사용자의 판단이나 실력을 재현한다고 보지 않는다.

## 클라우드 환경

Codex Cloud는 분석 코드와 테스트 작업에 사용하고, 게임 실행은 로컬 Windows에서 확인한다. GPU 학습 환경은 아직 구성하지 않았다.

GitHub 원격 저장소에 코드가 올라간 뒤 Codex의 `Work in > Cloud > Select environment > Create environment`에서 그 저장소를 선택한다. Python 3.11 이상, CMake와 C++17 컴파일러를 설치하도록 요청하고 설정 명령으로 다음을 사용한다.

```bash
bash scripts/cloud_setup.sh
```

이 명령은 가상 환경, Python 테스트와 BWAPI에 의존하지 않는 C++ 설정 테스트를 준비한다. 원본 리플레이와 `data/`는 Git에 없으므로 클라우드에서 분석하려면 사용할 데이터를 별도로 전달해야 한다. 저장소의 `AGENTS.md`에 이 프로젝트의 작업 규칙을 기록했다.

## 참고

- [BWAPI 공식 저장소와 실행 안내](https://github.com/bwapi/bwapi)
- [screp 리플레이 파서](https://github.com/icza/screp)
- [Pluto](https://github.com/tscmoo/pluto): 자가 대전 강화학습으로 만들어진 별도 봇이며 현재 공개 저장소는 실행 바이너리를 배포한다. 이 프로젝트는 Pluto의 가중치를 사용하지 않는다.
- [Codex Cloud 환경 안내](https://learn.chatgpt.com/docs/environments/cloud-environments)

외부 도구와 SDK는 각 원본 프로젝트의 라이선스를 따른다. 게임 실행 파일과 데이터는 이 저장소에 포함하지 않는다.
