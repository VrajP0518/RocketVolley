// Included in Game.cpp to exercise the real game without widening the public gameplay API.
// Constructor bypasses window/audio/assets/settings; all steps use the production Jolt world.
int Game::Impl::runHeadlessTests() {
    int failures = 0;
    const auto check = [&](bool condition, const char *name) {
        TraceLog(condition ? LOG_INFO : LOG_ERROR, "HEADLESS: %s: %s", condition ? "PASS" : "FAIL", name);
        if (!condition) ++failures;
    };
    const auto prepare = [&]() {
        automatedPlayer = false;
        powerupsEnabled = false;
        difficulty = Difficulty::Pro;
        startMatch();
        applyDifficultyPhysics();
        state = MatchState::Playing;
        serveInProgress = false;
        for (Car &car : cars) {
            resetCar(car, {30.0F + static_cast<float>(car.slot) * 4.0F, 4.0F, 30.0F}, 0.0F);
            physics.setGravityFactor(car.body, 0.0F);
        }
        physics.setTransform(ball, {0.0F, 5.0F, 8.0F}, {});
        physics.setLinearVelocity(ball, {});
        physics.step(FixedStep); // discard stale contacts from previous fixtures
        resetTouchSequence();
        rallyTouches = 0;
    };

    check(!IsWindowReady() && !audio.ready && smokeTestMode, "no graphics, audio or persistent profile access");
    prepare();
    physics.setTransform(ball, {0.0F, BallRadius + 0.11F, 8.0F}, {});
    physics.setLinearVelocity(ball, {});
    physics.step(FixedStep);
    check(!resolveCompetitiveBall() && score[1] == 0, "no premature point above the floor");
    physics.setLinearVelocity(ball, {0.0F, -35.0F, 0.0F});
    bool landed = false;
    for (int step = 0; step < 20 && !landed; ++step) {
        physics.step(FixedStep);
        landed = resolveCompetitiveBall();
    }
    check(landed && score[1] == 1 && state == MatchState::PointWon, "fast floor impact scores once on correct half");
    scorePoint(1);
    check(score[1] == 1, "duplicate point callback cannot double-score");
    finishPointSequence();
    check(state == MatchState::ServeCountdown && servingTeam == 1 && serveCountdown > 2.9F, "point transitions to winner serve");

    prepare();
    lastTouchTeam = 0;
    physics.setTransform(ball, {18.0F, 10.0F, -12.0F}, {});
    check(resolveCompetitiveBall() && score[1] == 1, "escaped ball faults last toucher regardless of half");
    prepare();
    score = {3, 2};
    matchTime = 0.0F;
    scorePoint(1);
    check(!pendingGameOver && overtime, "equalizing final rally enters overtime");
    finishPointSequence();
    launchServe();
    scorePoint(0);
    finishPointSequence();
    check(state == MatchState::GameOver && winner == 0, "overtime point finishes match");
    const int recordedMatches = careerMatches;
    recordCareerMatch();
    check(careerMatches == recordedMatches, "match progression committed once");

    prepare();
    physics.setTransform(ball, {12.8F, 3.0F, 8.0F}, {});
    physics.setLinearVelocity(ball, {25.0F, 0.0F, 0.0F});
    for (int i = 0; i < 12; ++i) physics.step(FixedStep);
    check(physics.linearVelocity(ball).x < 0.0F && physics.transform(ball).position.x < ArenaHalfWidth,
        "Jolt side wall contains fast ball");
    prepare();
    physics.setTransform(ball, {0.0F, 2.0F, 1.4F}, {});
    physics.setLinearVelocity(ball, {0.0F, 0.0F, -25.0F});
    for (int i = 0; i < 12; ++i) physics.step(FixedStep);
    check(physics.transform(ball).position.z > 0.0F, "Jolt net blocks a low fast ball");
    prepare();
    physics.setTransform(ball, {0.0F, 5.0F, 1.5F}, {});
    physics.setLinearVelocity(ball, {0.0F, 0.0F, -20.0F});
    for (int i = 0; i < 15; ++i) physics.step(FixedStep);
    check(physics.transform(ball).position.z < -0.5F, "legal ball clears net");

    prepare();
    resetCar(cars[0], {10.0F, 0.5F, 8.0F}, 0.0F);
    physics.setTransform(ball, {12.8F, 2.6F, 8.0F}, {});
    physics.setLinearVelocity(ball, {20.0F, 0.0F, 0.0F});
    previousBallVelocity = {20.0F, 0.0F, 0.0F};
    for (int i = 0; i < 5; ++i) {
        physics.step(FixedStep);
        checkBallImpact(physics.linearVelocity(ball));
    }
    check(rallyTouches == 0, "nearby wall bounce is not a car touch");
    prepare();
    resetCar(cars[0], {0.0F, 0.5F, 8.0F}, 0.0F);
    physics.setTransform(ball, {0.0F, 1.98F, 8.0F}, {});
    physics.setLinearVelocity(ball, {0.0F, -1.0F, 0.0F});
    for (int i = 0; i < 15; ++i) {
        physics.step(FixedStep);
        checkBallImpact(physics.linearVelocity(ball));
    }
    check(rallyTouches == 1 && teamTouches[0] == 1, "gentle sustained real contact counts once");
    prepare();
    resetCar(cars[3], {0.0F, 0.6F, -8.0F}, 0.0F);
    physics.setTransform(ball, {0.0F, 3.5F, -6.0F}, {});
    physics.step(FixedStep);
    const Vec3 untouchedVelocity = physics.linearVelocity(ball);
    checkBallImpact(untouchedVelocity);
    check(length(subtract(physics.linearVelocity(ball), untouchedVelocity)) < 0.0001F,
        "bot cannot return a nearby ball without contact");

    std::array<Vec3, 2> identicalHits{};
    std::array<float, 2> identicalHeadings{};
    for (int fixture = 0; fixture < 2; ++fixture) {
        prepare();
        // Compare human and bot on the same legal half, where the old bot-only
        // return assist would have activated (an opposite-half fixture would miss it).
        Car &car = cars[fixture == 0 ? 0 : 1];
        resetCar(car, {0.0F, 0.5F, 12.0F}, Pi);
        physics.setLinearVelocity(car.body, {0.0F, 0.0F, -12.0F});
        physics.setTransform(ball, {0.0F, 1.8F, 9.7F}, {});
        physics.setLinearVelocity(ball, {0.0F, -2.0F, 0.0F});
        previousBallVelocity = {0.0F, -2.0F, 0.0F};
        for (int step = 0; step < 12 && rallyTouches == 0; ++step) {
            physics.step(FixedStep);
            checkBallImpact(physics.linearVelocity(ball));
        }
        check(rallyTouches == 1, "parity fixture makes a real car-ball contact");
        identicalHits[fixture] = physics.linearVelocity(ball);
        resetCar(car, {0.0F, 0.5F, 12.0F}, 0.0F);
        Controls controls;
        controls.throttle = 1.0F;
        controls.steer = 0.7F;
        driveCar(car, controls, FixedStep);
        identicalHeadings[fixture] = car.heading;
    }
    check(length(subtract(identicalHits[0], identicalHits[1])) < 0.001F,
        "identical human and AI impacts produce identical ball velocity");
    check(std::abs(identicalHeadings[0] - identicalHeadings[1]) < 0.00001F,
        "AI and human steering share the same physical turn rate");

    const auto wideFraming = ballCameraFraming({0.0F, 5.0F, 10.0F}, {-4.0F, 1.0F, 0.0F},
        {4.0F, 2.0F, 0.0F}, 16.0F / 9.0F, 58.0F);
    const auto narrowFraming = ballCameraFraming(wideFraming.position, {-4.0F, 1.0F, 0.0F},
        {4.0F, 2.0F, 0.0F}, 8.0F / 9.0F, 58.0F);
    check(narrowFraming.fov > wideFraming.fov && narrowFraming.fov <= 82.0F,
        "portrait split-screen framing accounts for reduced horizontal visibility");
    const auto overhead = ballCameraFraming({0.0F, 5.0F, 0.0F}, {0.0F, 0.5F, 0.0F},
        {0.0F, 10.0F, 0.0F}, 8.0F / 9.0F, 70.0F);
    check(std::isfinite(overhead.fov) && Vector3Distance(overhead.position, overhead.target) > 1.0F,
        "opposite framing rays cannot produce a zero camera direction");
    for (CameraMode mode : {CameraMode::Car, CameraMode::Ball}) {
        for (auto layout : {GameplayCameraLayout::Standard, GameplayCameraLayout::SplitScreen}) {
            const auto pose = gameplayCameraPose({12.0F, 0.5F, 20.0F}, {}, Pi,
                {0.0F, 8.0F, 0.0F}, mode, layout);
            check(pose.position.y >= 7.5F && Vector3Distance(pose.position, {12.0F, 0.5F, 20.0F}) > 8.0F,
                "near-wall camera clears cage without collapsing follow distance");
        }
    }

    prepare();
    resetCar(cars[0], {0.0F, 0.5F, 12.0F}, 0.0F);
    Controls steerLeft;
    steerLeft.throttle = 1.0F;
    steerLeft.steer = 1.0F;
    driveCar(cars[0], steerLeft, FixedStep);
    check(cars[0].heading < 0.0F, "left steering agrees with left dodge");
    resetCar(cars[0], {0.0F, 0.5F, 12.0F}, 0.0F);
    steerLeft.throttle = -1.0F;
    driveCar(cars[0], steerLeft, FixedStep);
    check(cars[0].heading > 0.0F, "reverse steering reverses yaw");
    resetCar(cars[0], {0.0F, 0.5F, 12.0F}, 0.0F);
    physics.setTransform(cars[0].body, {0.0F, 0.5F, 12.0F}, {0.0F, 0.0F, 1.0F, 0.0F});
    Controls recover;
    recover.jumpPressed = true;
    driveCar(cars[0], recover, FixedStep);
    check(physics.transform(cars[0].body).rotation.w > 0.9F && physics.linearVelocity(cars[0].body).y > 0.0F,
        "jump recovers overturned car");

    startTraining();
    state = MatchState::Playing;
    accumulator = 0.0F;
    resetCar(cars[0], {0.0F, 0.5F, 12.0F}, 0.0F);
    Controls jumpEdge;
    jumpEdge.jumpPressed = true;
    advanceSimulation(0.002F, jumpEdge);
    check(cars[0].jumpsUsed == 0, "render-only frame queues jump without stepping");
    advanceSimulation(0.007F, {});
    check(cars[0].jumpsUsed == 1, "queued jump reaches next physics step");
    advanceSimulation(0.05F, {});
    check(cars[0].jumpsUsed == 1, "catch-up steps do not replay jump edge");
    resetTrainingServe();
    serveCountdown = 0.001F;
    advanceSimulation(0.05F, {});
    check(state == MatchState::Playing && accumulator >= 0.0F, "kickoff transition never makes accumulator negative");

    prepare();
    resetCar(cars[0], {0.0F, 0.5F, 12.0F}, 0.0F);
    cars[0].aiTarget = {3.0F, 0.0F, 8.0F};
    cars[0].aiThinkTimer = 1.0F;
    const Controls retreat = aiControls(cars[0], FixedStep);
    check(retreat.throttle < 0.0F && retreat.steer < 0.0F,
        "retreating AI steers its rear toward the interception lane");

    // Compare independent prediction against the actual Jolt court over short free-flight/wall cases.
    bool predictionAgrees = true;
    for (const BallKinematics initial : {BallKinematics{{0.0F, 6.0F, 10.0F}, {3.0F, 2.0F, -4.0F}},
            BallKinematics{{12.0F, 4.0F, 8.0F}, {10.0F, 1.0F, 0.0F}}}) {
        prepare();
        physics.setTransform(ball, initial.position, {});
        physics.setLinearVelocity(ball, initial.velocity);
        const auto predicted = predictBallMotion(initial, 0.25F, 18.0F, ballElasticity);
        for (int i = 0; i < 30; ++i) physics.step(FixedStep);
        predictionAgrees = predictionAgrees && length(subtract(predicted.position, physics.transform(ball).position)) < 0.55F;
    }
    check(predictionAgrees, "prediction approximately matches Jolt free-flight and wall bounce");

    startTraining();
    const Vec3 trainingFeedStart = servePosition;
    const Vec3 trainingFeedVelocity = serveVelocity;
    resetTrainingServe(true);
    check(length(subtract(servePosition, trainingFeedStart)) < 0.001F
        && length(subtract(serveVelocity, trainingFeedVelocity)) < 0.001F, "manual training retry repeats exact feed");
    const int attempts = trainingAttempts;
    state = MatchState::Playing;
    resolvePracticeLanding({0.0F, BallRadius, 8.0F});
    for (int i = 0; i < 200 && state == MatchState::Playing; ++i) fixedUpdate({});
    check(state == MatchState::ServeCountdown && trainingAttempts == attempts + 1 && serveCountdown <= 1.01F,
        "practice retries promptly without match score");
    check(score[0] == 0 && score[1] == 0, "practice floor does not score competitive points");

    // UC-01: every practice feed reaches the learner, including Rookie's speed cap.
    for (Difficulty level : {Difficulty::Rookie, Difficulty::Pro}) {
        for (TrainingFeed feed : {TrainingFeed::Lob, TrainingFeed::Fast, TrainingFeed::CrossCourt}) {
            SetRandomSeed(42);
            difficulty = level;
            trainingFeed = feed;
            startTraining();
            launchServe();
            resetCar(cars[0], {35.0F, 4.0F, 30.0F}, 0.0F);
            physics.setGravityFactor(cars[0].body, 0.0F);
            bool reachedPlayerHalf = false;
            for (int step = 0; step < 960 && !trainingBallHasTouchedGround; ++step) {
                fixedUpdate({});
                const Vec3 position = physics.transform(ball).position;
                reachedPlayerHalf = reachedPlayerHalf || (position.z > 4.0F && position.y > BallRadius);
            }
            TraceLog(LOG_INFO, "HEADLESS: practice feed=%d difficulty=%d landing_z=%.2f",
                static_cast<int>(feed), static_cast<int>(level), physics.transform(ball).position.z);
            check(reachedPlayerHalf && trainingBallHasTouchedGround && physics.transform(ball).position.z > 4.0F,
                "each practice feed clears net and lands on learner half");
        }
    }

    // UC-02: a complete challenge counts each shot once and ends on shot ten.
    startTargetChallenge();
    for (int shot = 0; shot < TargetChallengeShots; ++shot) {
        launchServe();
        trainingReturnSuccessful = true;
        resolvePracticeLanding(challengeTarget);
        const int earned = challengeScore;
        resolvePracticeLanding(challengeTarget);
        check(challengeScore == earned, "challenge landing cannot award points twice");
        for (int step = 0; step < 200 && state == MatchState::Playing; ++step) fixedUpdate({});
        check(shot == TargetChallengeShots - 1 ? state == MatchState::GameOver
            : state == MatchState::ServeCountdown, "challenge advances and ends after exactly ten shots");
    }
    check(trainingAttempts == TargetChallengeShots && challengeTargetsHit == TargetChallengeShots
        && challengeBestCombo == TargetChallengeShots && challengeBestScore >= challengeScore,
        "challenge commits ten-shot score and combo record");
    startTargetChallenge();
    launchServe();
    trainingReturnSuccessful = true;
    resolvePracticeLanding(challengeTarget);
    const int scoreBeforeMiss = challengeScore;
    resetTrainingServe();
    launchServe();
    resolvePracticeLanding({0.0F, BallRadius, 8.0F});
    check(challengeCombo == 0 && challengeBestCombo == 1 && challengeTargetsHit == 1
        && challengeScore == scoreBeforeMiss, "miss breaks challenge combo without erasing earned points");

    for (Difficulty level : {Difficulty::Rookie, Difficulty::Pro}) {
        difficulty = level;
        academyActive = true;
        academyLesson = AcademyLesson::AerialReturn;
        setupAcademyLesson();
        const auto feed = predictBallFlight({servePosition, serveVelocity}, level == Difficulty::Rookie ? 10.44F : 18.0F, ballElasticity);
        check(feed.landed && feed.landing().z > 4.0F, "Academy aerial feed clears net on both difficulties");
        academyLesson = AcademyLesson::TargetLanding;
        setupAcademyLesson();
        const auto targetFeed = predictBallFlight({servePosition, serveVelocity}, level == Difficulty::Rookie ? 10.44F : 18.0F, ballElasticity);
        check(targetFeed.landed && targetFeed.landing().z > 4.0F, "Academy placement feed reaches player half");
    }

    for (Difficulty level : {Difficulty::Rookie, Difficulty::Pro}) {
        for (GameMode mode : {GameMode::Match, GameMode::ThreeVsThree}) {
            // Keep each scenario independent of cosmetic random draws in earlier scenarios.
            SetRandomSeed(20260917U + static_cast<unsigned>(mode) + static_cast<unsigned>(level) * 10U);
            difficulty = level;
            applyDifficultyPhysics();
            startMatch(mode);
            automatedPlayer = true;
            bool finite = true;
            int touches = 0;
            int points = 0;
            int exchanges = 0;
            int rallyExchanges = 0;
            int bestExchanges = 0;
            int previousTouchTeam = -1;
            for (int step = 0; step < 7200; ++step) {
                totalTime += FixedStep;
                const int touchesBefore = rallyTouches;
                if (state == MatchState::ServeCountdown) countdownFixedUpdate({});
                else if (state == MatchState::Playing) fixedUpdate({});
                else if (state == MatchState::PointWon) {
                    touches += rallyTouches;
                    ++points;
                    finishPointSequence();
                    previousTouchTeam = -1;
                    rallyExchanges = 0;
                } else if (state == MatchState::GameOver) break;
                if (rallyTouches > touchesBefore) {
                    if (previousTouchTeam >= 0 && lastTouchTeam != previousTouchTeam) {
                        ++exchanges;
                        bestExchanges = std::max(bestExchanges, ++rallyExchanges);
                    }
                    previousTouchTeam = lastTouchTeam;
                }
                updateParticles(FixedStep);
                const Vec3 position = physics.transform(ball).position;
                finite = finite && std::isfinite(position.x) && std::isfinite(position.y) && std::isfinite(position.z);
                for (const Car &car : cars) {
                    const Vec3 velocity = physics.linearVelocity(car.body);
                    finite = finite && std::isfinite(length(velocity)) && car.boost >= 0.0F && car.boost <= 100.0F;
                }
            }
            TraceLog(LOG_INFO, "HEADLESS: simulation mode=%d difficulty=%d touches=%d points=%d best_rally=%d exchanges=%d best_exchanges=%d",
                static_cast<int>(mode), static_cast<int>(level), touches, points, bestRallyTouches, exchanges, bestExchanges);
            check(bestRallyTouches >= 3 && exchanges >= 2,
                "seeded teams sustain real exchanges between opposing teams");
            check(finite && (points > 0 || rallyTouches >= 4) && touches + rallyTouches > 0, "60-second seeded AI match progresses with physical contacts and finite state");
        }
    }
    // UC-06: interruptions must not cost a point or resume without the player.
    startMatch(GameMode::LocalCoop);
    state = MatchState::Playing;
    controllerWasConnected = true;
    handlePauseInput(false, false, false, true);
    check(state == MatchState::Paused && pausedFrom == MatchState::Playing,
        "losing window focus pauses the live rally");
    state = MatchState::Playing;
    controllerWasConnected = true;
    handlePauseInput(false, true, true, false);
    check(state == MatchState::Paused, "disconnect wins over simultaneous pause button");
    state = MatchState::Paused;
    pausedFrom = MatchState::ServeCountdown;
    handlePauseInput(false, true, true, false);
    check(state == MatchState::Paused, "co-op cannot resume while P2 is disconnected");
    handlePauseInput(false, false, true, true);
    check(state == MatchState::Paused, "reconnecting never resumes automatically");
    handlePauseInput(false, true, true, true);
    check(state == MatchState::ServeCountdown, "explicit resume restores countdown after reconnection");
    controllerWasConnected = false;
    handlePauseInput(false, false, true, false);
    check(state == MatchState::Paused, "co-op kickoff waits even if P2 was never connected");
    handlePauseInput(false, true, true, true);
    handlePauseInput(true, false, true, true);
    const float countdownBeforePause = serveCountdown;
    const auto tickBeforePause = simulationTick;
    advanceSimulation(0.1F, jumpEdge);
    check(showHelp && state == MatchState::Paused && serveCountdown == countdownBeforePause
        && simulationTick == tickBeforePause && inputEdges[0].pending == 0,
        "help freezes countdown and discards gameplay input");
    handlePauseInput(true, false, true, true);
    check(!showHelp && state == MatchState::ServeCountdown, "closing help resumes only its own pause");
    handlePauseInput(false, true, true, true);
    handlePauseInput(true, false, true, true);
    handlePauseInput(true, false, true, true);
    check(state == MatchState::Paused, "help opened from manual pause does not unpause");
    state = MatchState::ServeCountdown;
    showHelp = true;
    handlePauseInput(false, false, true, true);
    check(state == MatchState::Paused && helpPausedGame, "help from loading blocks the next kickoff");
    handlePauseInput(true, false, true, false);
    check(state == MatchState::Paused && !showHelp, "closing help cannot bypass missing P2");
    gameMode = GameMode::Match;
    handlePauseInput(false, true, true, false);
    check(state == MatchState::ServeCountdown, "solo player can resume on keyboard after controller loss");

    // UC-07: long rallies retain exactly the latest five seconds in order.
    replayFrames.clear();
    for (int frame = 0; frame < 650; ++frame) {
        physics.setTransform(ball, {static_cast<float>(frame), 5.0F, 8.0F}, {});
        captureReplayFrame(true);
    }
    replayPlaybackFrame = 0.0F;
    check(replayFrames.size() == 300 && currentReplayFrame()->ball.position.x == 350.0F,
        "replay evicts oldest frames while preserving chronological order");
    replayPlaybackFrame = 999.0F;
    check(currentReplayFrame()->ball.position.x == 649.0F, "replay clamps playback to newest retained frame");
    resetRound(0);
    check(currentReplayFrame() == nullptr, "new rally clears prior replay history");

    ReplayFrame replayFrom;
    replayFrom.ball = {{0.0F, 4.0F, 8.0F}, {0.0F, 0.7071068F, 0.0F, 0.7071068F}};
    replayFrom.cars[0] = {{1.0F, 0.5F, 10.0F}, {}};
    replayFrom.visibleCars = 1;
    ReplayFrame replayTo = replayFrom;
    replayTo.ball.position.x = 2.0F;
    replayTo.ball.rotation = {0.0F, -0.7071068F, 0.0F, -0.7071068F};
    replayTo.cars[0].position = {40.0F, 4.0F, 30.0F};
    replayTo.visibleCars = 0;
    replayFrames = {replayFrom, replayTo};
    replayPlaybackFrame = 0.5F;
    const auto smoothReplay = sampledReplayFrame();
    check(std::abs(smoothReplay.ball.position.x - 1.0F) < 0.0001F
        && std::abs(smoothReplay.ball.rotation.w) > 0.7F,
        "replay interpolates motion and equivalent quaternion signs without a spin");
    check(smoothReplay.cars[0].position.x == 1.0F && smoothReplay.visibleCars == 1,
        "replay does not interpolate a demolition teleport across the court");
    replayPlaybackFrame = 1.0F;
    check(sampledReplayFrame().visibleCars == 0, "replay preserves recorded respawn visibility");
    replayPlaybackFrame = 1.0e30F;
    check(currentReplayFrame()->ball.position.x == 2.0F && sampledReplayFrame().ball.position.x == 2.0F,
        "replay clamps extreme playback positions before converting to an index");
    resetRound(0);

    // PB files and playback retain the same on-disk format, including equivalent rotations.
    const std::vector<AcademyGhostFrame> ghostFrames{
        {0.0F, AcademyLesson::BoostGates, replayFrom.ball},
        {1.0F, AcademyLesson::BoostGates, replayTo.ball}};
    std::vector<AcademyGhostFrame> decodedGhost;
    int decodedTime = 0;
    check(decodeAcademyGhost(encodeAcademyGhost(ghostFrames, 100), decodedGhost, decodedTime)
        && decodedGhost.size() == 2 && decodedTime == 100, "production PB ghost codec round-trips headlessly");
    academyActive = true;
    academyGhostEnabled = true;
    academyLesson = AcademyLesson::BoostGates;
    academyBestGhost = decodedGhost;
    academyRunTimer = 0.5F;
    Transform ghostAtHalf;
    check(academyGhostTransform(ghostAtHalf) && std::abs(ghostAtHalf.position.x - 1.0F) < 0.0001F
        && std::abs(ghostAtHalf.rotation.w) > 0.7F,
        "PB playback takes shortest rotation path across quaternion sign changes");
    for (float invalidTime : {-0.5F, 1.5F}) {
        auto invalidGhost = ghostFrames;
        invalidGhost[invalidTime < 0.0F ? 0 : 1].time = invalidTime;
        check(!decodeAcademyGhost(encodeAcademyGhost(invalidGhost, 100), decodedGhost, decodedTime)
            && decodedGhost.size() == 2 && decodedTime == 100,
            "invalid ghost timeline is rejected without replacing valid decoded data");
    }
    academyActive = false;

    prepare();
    for (std::size_t pad = 0; pad < BoostPadPositions.size(); ++pad)
        boostPadRespawnTimers[pad] = BoostPadPositions[pad].z > 0.0F ? 8.0F : 0.0F;
    check(nearestAvailableBoostPad({0.0F, 0.5F, 3.0F}, 0) == -1,
        "defender cannot select an unreachable enemy-half boost pad");
    prepare();
    thirdTouchBoostTimer = 0.5F;
    for (int step = 0; step < 12; ++step) fixedUpdate({});
    check(std::abs(thirdTouchBoostTimer - 0.4F) < 0.0001F,
        "third-touch power duration advances with simulation without rendering");
    state = MatchState::Paused;
    const float pausedPowerDuration = thirdTouchBoostTimer;
    advanceSimulation(0.1F, {});
    check(thirdTouchBoostTimer == pausedPowerDuration,
        "paused simulation preserves third-touch power duration");

    prepare();
    SetRandomSeed(8128);
    resetRound(0);
    const Vec3 expectedServe = servePosition;
    const Vec3 expectedTarget = serveLandingTarget;
    SetRandomSeed(8128);
    emitBurst({0.0F, 1.0F, 0.0F}, GOLD, 80, 3.0F);
    for (int frame = 0; frame < 60; ++frame) {
        shake = 0.2F;
        updateCamera(1.0F / 144.0F);
    }
    resetRound(0);
    check(length(subtract(servePosition, expectedServe)) < 0.0001F
        && length(subtract(serveLandingTarget, expectedTarget)) < 0.0001F,
        "particles and different camera frame counts cannot change seeded serves");
    particles.clear();
    prepare();
    resetCar(cars[0], {0.0F, 0.5F, 12.0F}, 0.0F);
    cars[0].airborneTime = 0.4F;
    cars[0].landingImpact = 8.0F;
    particles.clear();
    driveCar(cars[0], {}, FixedStep);
    const auto landingParticles = particles.size();
    driveCar(cars[0], {}, FixedStep);
    check(landingParticles == 6 && particles.size() == landingParticles && cars[0].landingImpact == 0.0F,
        "hard landing emits feedback once and rearms only after another jump");

    std::array<float, 2> flightPeaks{};
    for (int cadence = 0; cadence < 2; ++cadence) {
        prepare();
        state = MatchState::ServeCountdown;
        serveCountdown = 99.0F;
        servePosition = {0.0F, 12.0F, -16.0F};
        resetCar(cars[0], {0.0F, 0.62F, 10.0F}, Pi);
        accumulator = 0.0F;
        scriptedAerialTest = true;
        aerialTestTimer = aerialPeakHeight = aerialPeakForwardY = 0.0F;
        aerialFirstJumpTriggered = aerialSecondJumpTriggered = aerialTestComplete = false;
        aerialMaxJumpsUsed = 0;
        const int ticksPerFrame = cadence == 0 ? 2 : 5;
        for (int frame = 0; frame < 320 / ticksPerFrame; ++frame)
            advanceSimulation(FixedStep * static_cast<float>(ticksPerFrame), {});
        flightPeaks[cadence] = aerialPeakHeight;
        check(aerialTestComplete && aerialPeakHeight >= 6.0F && aerialMaxJumpsUsed == 2,
            "fixed-step scripted double-jump and aerial boost reach expected height");
        scriptedAerialTest = false;
    }
    check(std::abs(flightPeaks[0] - flightPeaks[1]) < 0.02F,
        "scripted flight remains consistent at different render cadences");

    // UC-09: driving bindings take priority over optional gameplay shortcuts.
    const auto savedBindings = bindings;
    for (int key : {KEY_ONE, KEY_TWO, KEY_TAB, KEY_G, KEY_LEFT_BRACKET, KEY_RIGHT_BRACKET, KEY_L, KEY_F2}) {
        bindings = DefaultBindings;
        check(gameplayShortcutPressed(key, true) && !gameplayShortcutPressed(key, false),
            "unbound shortcut fires only on a press");
        const bool assigned = assignBinding(static_cast<std::size_t>(BindAction::Forward), key);
        check(assigned && boundKey(BindAction::Forward) == key && !gameplayShortcutPressed(key, true),
            "custom driving key cannot also change gameplay settings or training");
    }
    bindings = DefaultBindings;
    check(assignBinding(static_cast<std::size_t>(BindAction::Forward), KEY_S)
        && boundKey(BindAction::Reverse) == KEY_W && bindingsAreUnique(bindings),
        "rebinding an occupied key swaps actions without duplicates");
    for (int reserved : {KEY_F1, KEY_ENTER}) {
        const auto before = bindings;
        check(!assignBinding(static_cast<std::size_t>(BindAction::Forward), reserved) && bindings == before,
            "reserved help/menu keys cannot change controls");
        auto invalidProfile = DefaultBindings;
        invalidProfile[static_cast<std::size_t>(BindAction::Forward)] = reserved;
        check(!bindingsAreUnique(invalidProfile), "profile validation also rejects reserved-key bindings");
    }
    bindings = savedBindings;

    // UC-12: a locked drill repeats automatically and scores only completed shots.
    startTraining();
    handleTrainingCommands(false, true, false);
    const Vec3 lockedPosition = servePosition;
    const Vec3 lockedVelocity = serveVelocity;
    const auto lockedFeed = currentTrainingFeed;
    handleTrainingCommands(false, false, true);
    check(trainingRepeatShot && trainingCompleted == 0 && trainingReturns == 0,
        "manual drill retries do not count as completed attempts");
    for (int attempt = 0; attempt < 3; ++attempt) {
        launchServe();
        trainingReturnSuccessful = attempt < 2;
        resolvePracticeLanding({0.0F, BallRadius, attempt < 2 ? -8.0F : 8.0F});
        resolvePracticeLanding({0.0F, BallRadius, -8.0F}); // duplicate callback
        for (int step = 0; step < 200 && state == MatchState::Playing; ++step) fixedUpdate({});
        check(state == MatchState::ServeCountdown && currentTrainingFeed == lockedFeed
            && length(subtract(servePosition, lockedPosition)) < 0.001F
            && length(subtract(serveVelocity, lockedVelocity)) < 0.001F,
            "locked drill automatically repeats the exact shot after success or miss");
    }
    check(trainingCompleted == 3 && trainingReturns == 2 && trainingStreak == 0 && trainingBestStreak == 2,
        "completed-shot accuracy and best streak survive a miss without double counting");
    const auto feedBeforeChange = trainingFeed;
    handleTrainingCommands(true, false, false);
    check(trainingFeed != feedBeforeChange && trainingRepeatShot && trainingCompleted == 3,
        "changing feed keeps drill lock and session statistics");
    startTraining();
    check(!trainingRepeatShot && trainingCompleted == 0 && trainingBestStreak == 0,
        "new training session resets drill statistics and lock");
    startTargetChallenge();
    const auto challengeFeedBefore = trainingFeed;
    handleTrainingCommands(true, true, true);
    check(trainingAttempts == 1 && trainingFeed == challengeFeedBefore && !trainingRepeatShot,
        "free-training commands cannot alter a scored challenge");

    // UC-13: preferences apply without resuming play or requiring an audio device.
    const auto savedPreferences = preferences;
    state = MatchState::Paused;
    pausedFrom = MatchState::Playing;
    const float pausedClock = matchTime;
    settingsNotice = "OLD MATCH RESULT";
    settingsNoticeTimer = 2.0F;
    openPreferences(true);
    check(settingsNotice.empty() && settingsNoticeTimer == 0.0F, "options do not display stale match-result notices");
    preferencesMenuIndex = 0;
    for (int i = 0; i < 15; ++i) cyclePreferences(-1);
    check(preferences.musicVolume == 0 && audio.musicGain == 0.0F && audio.effectsGain == 1.0F,
        "music can mute independently and clamps at zero");
    preferencesMenuIndex = 1;
    cyclePreferences(-1);
    check(preferences.effectsVolume == 90 && std::abs(audio.effectsGain - 0.9F) < 0.001F,
        "effects mix updates independently without an audio device");
    advanceSimulation(0.1F, jumpEdge);
    closePreferences();
    check(state == MatchState::Paused && matchTime == pausedClock && !preferencesFromPause,
        "closing preferences returns to pause without advancing the match");
    preferencesMenuIndex = 4;
    cyclePreferences(1);
    check(preferences.musicVolume == 100 && preferences.effectsVolume == 100 && preferences.cameraShake == 100,
        "restore preferences resets only audio and comfort");
    state = MatchState::Playing;
    preferences.cameraShake = 0;
    camera.position = {0.0F, 8.0F, 18.0F};
    camera.target = {0.0F, 1.5F, 2.0F};
    camera.fovy = 58.0F;
    const auto baselineCamera = camera;
    shake = 0.0F;
    updateCamera(0.1F);
    const auto unshaken = camera;
    camera = baselineCamera;
    shake = 1.0F;
    updateCamera(0.1F);
    check(Vector3Distance(camera.position, unshaken.position) < 0.0001F,
        "zero camera-shake preference removes impact jitter");
    preferences = savedPreferences;
    audio.applyMix(preferences.musicGain(), preferences.effectsGain());

    // Exercise the production profile codec without reading/writing a real profile.
    const std::string profileBeforeTest = encodeSettings();
    preferences = {35, 65, 0, false};
    bindings = DefaultBindings;
    assignBinding(static_cast<std::size_t>(BindAction::Forward), KEY_L);
    careerXp = 4321;
    careerMatches = 12;
    careerWins = 8;
    careerMilestones = (1 << CareerMilestoneCount) - 1;
    academyBestTimeCentiseconds = 5432;
    challengeBestScore = 900;
    const std::string encodedProfile = encodeSettings();
    preferences = {};
    bindings = DefaultBindings;
    careerXp = 0;
    std::istringstream profileInput(encodedProfile);
    readSettings(profileInput);
    check(encodeSettings() == encodedProfile && preferences.musicVolume == 35
        && preferences.effectsVolume == 65 && preferences.cameraShake == 0 && !preferences.pointReplays
        && boundKey(BindAction::Forward) == KEY_L && careerXp == 4321 && careerWins == 8
        && academyBestTimeCentiseconds == 5432 && challengeBestScore == 900,
        "production profile round-trip preserves preferences, bindings and earned records");
    std::istringstream currentProfile(encodedProfile);
    std::ostringstream legacyProfile;
    std::string profileLine;
    while (std::getline(currentProfile, profileLine)) {
        if (profileLine.rfind("music_volume=", 0) != 0 && profileLine.rfind("effects_volume=", 0) != 0
            && profileLine.rfind("camera_shake=", 0) != 0 && profileLine.rfind("point_replays=", 0) != 0) legacyProfile << profileLine << '\n';
    }
    preferences = {};
    std::istringstream legacyInput(legacyProfile.str());
    readSettings(legacyInput);
    check(preferences.musicVolume == 100 && preferences.effectsVolume == 100 && preferences.cameraShake == 100
        && preferences.pointReplays && careerXp == 4321 && boundKey(BindAction::Forward) == KEY_L,
        "legacy version-three profile keeps records and receives original audio defaults");
    std::istringstream malformed("music_volume=70garbage\neffects_volume=9999999999999999999\ncamera_shake=-10\nunknown_key=8\n");
    readSettings(malformed);
    check(preferences.musicVolume == 100 && preferences.effectsVolume == 100 && preferences.cameraShake == 0
        && careerXp == 4321, "malformed preference values are ignored and valid out-of-range values clamp");
    std::istringstream restoreProfile(profileBeforeTest);
    readSettings(restoreProfile);

    // UC-14: disabling replays keeps the point celebration and all match rules.
    preferences.pointReplays = false;
    startMatch();
    state = MatchState::Playing;
    scorePoint(0);
    replayFrames.resize(60);
    advancePointPresentation(0.2F);
    check(state == MatchState::PointWon, "replays-off retains the scoring celebration");
    advancePointPresentation(1.0F);
    check(state == MatchState::ServeCountdown && servingTeam == 0 && score[0] == 1,
        "replays-off proceeds directly to winner kickoff without altering score");
    preferences.pointReplays = true;
    state = MatchState::Playing;
    scorePoint(1);
    replayFrames.resize(60);
    advancePointPresentation(1.0F);
    check(state == MatchState::GoalReplay, "replays-on keeps instant replay flow");
    advancePointPresentation(2.0F);
    check(state == MatchState::ServeCountdown && servingTeam == 1, "enabled replay finishes into next kickoff");
    preferences.pointReplays = false;
    score = {scoreLimit - 1, 0};
    state = MatchState::Playing;
    scorePoint(0);
    advancePointPresentation(1.0F);
    check(state == MatchState::GameOver && winner == 0, "replays-off still reaches final results");
    preferences = savedPreferences;

    TraceLog(LOG_INFO, "HEADLESS: %d failure(s)", failures);
    return failures == 0 ? 0 : 1;
}
