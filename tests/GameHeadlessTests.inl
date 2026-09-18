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
    check(!tryAiBallTouch(cars[3]), "bot cannot return a nearby ball without contact");

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
            for (int step = 0; step < 7200; ++step) {
                totalTime += FixedStep;
                if (state == MatchState::ServeCountdown) countdownFixedUpdate({});
                else if (state == MatchState::Playing) fixedUpdate({});
                else if (state == MatchState::PointWon) {
                    touches += rallyTouches;
                    ++points;
                    finishPointSequence();
                } else if (state == MatchState::GameOver) break;
                updateParticles(FixedStep);
                const Vec3 position = physics.transform(ball).position;
                finite = finite && std::isfinite(position.x) && std::isfinite(position.y) && std::isfinite(position.z);
                for (const Car &car : cars) {
                    const Vec3 velocity = physics.linearVelocity(car.body);
                    finite = finite && std::isfinite(length(velocity)) && car.boost >= 0.0F && car.boost <= 100.0F;
                }
            }
            TraceLog(LOG_INFO, "HEADLESS: simulation mode=%d difficulty=%d touches=%d points=%d best_rally=%d",
                static_cast<int>(mode), static_cast<int>(level), touches, points, bestRallyTouches);
            check(bestRallyTouches >= 3, "seeded teams sustain multi-return rallies");
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

    TraceLog(LOG_INFO, "HEADLESS: %d failure(s)", failures);
    return failures == 0 ? 0 : 1;
}
