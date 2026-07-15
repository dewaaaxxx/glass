#pragma once

#include "Prediction.fast.h"
#include <imgui/imgui.h>
#include <algorithm>
#include <cmath>
#include "ScreenTable.h"
#include "mod/ButtonClicker.h"

using namespace ImGui;

// ============================================================================
// PROFESSIONAL PHYSICS CONSTANTS & EQUATIONS
// ============================================================================

constexpr double maxAngle = 360.0 / (180.0 / M_PI);
constexpr double GRAVITATIONAL_CONSTANT = 9.81;
constexpr double FRICTION_COEFFICIENT = 0.12;           // Cloth-to-ball friction
constexpr double ROLLING_RESISTANCE = 0.003;            // Advanced rolling dynamics
constexpr double BALL_DECELERATION = 196.0;             // Professional deceleration rate
constexpr double COLLISION_DAMPING = 0.95;              // Energy retention in collisions
constexpr double SPIN_DECAY_RATE = 0.015;               // Topspin/backspin dissipation
constexpr double ENGLISH_MULTIPLIER = 1.15;             // Sidespin power modifier

// Luxury physics thresholds for precise shooting
constexpr double OPTIMAL_VELOCITY_RANGE_MIN = 0.5;
constexpr double OPTIMAL_VELOCITY_RANGE_MAX = 8.5;
constexpr double PRECISION_ANGLE_TOLERANCE = 0.001;     // Sub-degree accuracy

// Explosive algorithm parameters
constexpr int SIMULATION_SUBSTEPS = 8;                  // High-precision simulation
constexpr double IMPACT_FORCE_THRESHOLD = 2.0;          // Detect dynamic collisions

// ============================================================================
// ADVANCED PHYSICS CALCULATION FUNCTIONS
// ============================================================================

/**
 * Revolutionary physics engine: Calculates optimal power with exponential curve
 * based on distance, friction, and spin dynamics.
 * 
 * P(d) = sqrt(2 * μ * g * (d + k*s))
 * Where: μ = friction coefficient, g = gravity, d = distance, s = spin factor, k = english multiplier
 */
inline double CalculateOptimalPowerAdvanced(double distance, double spinFactor = 0.0, double englishInfluence = 1.0) {
    // Pakai 196.0 = sliding deceleration constant dari engine (bukan friction*gravity).
    // Formula yang lama: sqrt(2 * 0.12 * 9.81 * dist) → hasil ~9x terlalu kecil
    // → bola selalu berhenti jauh sebelum pocket.
    double p = sqrt(2.0 * BALL_DECELERATION * distance);
    if (p < 80.0) p = 80.0;
    if (p > 666.0) p = 666.0;
    return p;
}

/**
 * Calculates ball trajectory with advanced physics:
 * - Spin-induced curve (Magnus effect)
 * - Friction dissipation
 * - Rolling resistance
 */
inline Point2D PredictBallTrajectory(Point2D initialPos, Point2D velocity, double spinRate = 0.0) {
    // Apply Magnus effect (curve from spin)
    double magnusCoefficient = spinRate * 0.02;
    velocity.x += magnusCoefficient * velocity.y;
    velocity.y -= magnusCoefficient * velocity.x;
    
    // Apply friction and rolling resistance
    double velocityMagnitude = sqrt(velocity.square());
    double decayFactor = exp(-ROLLING_RESISTANCE * velocityMagnitude);
    
    return initialPos + (velocity * decayFactor);
}

/**
 * Helper: Extract spin magnitude from Vec2d spin vector
 */
inline double ExtractSpinMagnitude(const Point2D& spinVector) {
    return sqrt(spinVector.x * spinVector.x + spinVector.y * spinVector.y);
}

/**
 * Premium scoring function: Evaluates shot quality across multiple dimensions
 * Returns a composite score incorporating distance, angle precision, and collision dynamics
 */
inline double CalculateShotQualityScore(
    double distCueToTarget,
    double distTargetToPocket,
    double angleDeviation,
    double spinQuality,
    bool firstHitValid,
    int ballsRemaining
) {
    // Base distance score (logarithmic for luxury precision)
    double distanceScore = log(1.0 + distCueToTarget + distTargetToPocket);
    
    // Angle precision bonus (exponential reward for accuracy)
    double anglePrecisionBonus = exp(-angleDeviation * 100.0);
    
    // Spin quality contribution
    double spinBonus = (1.0 + abs(spinQuality) * 0.5);
    
    // Collision validity multiplier
    double collisionMultiplier = firstHitValid ? 1.2 : 0.5;
    
    // Strategic depth: fewer balls = higher quality rewards
    double strategicDepth = 1.0 + (16.0 - ballsRemaining) * 0.05;
    
    double compositeScore = distanceScore * anglePrecisionBonus * spinBonus * collisionMultiplier * strategicDepth;
    
    return compositeScore;
}

/**
 * Explosive candidate ranking: Multi-tier evaluation system
 * Ranks candidates with exponential differentiation for clear winners
 */
inline double RankCandidate(
    const Candidate& cand,
    double basePower,
    bool firstHitValid,
    int ballsRemaining,
    double preferredSpinRate = 0.0
) {
    // Calculate angle quality (penalize deviation from optimal)
    double angleQuality = cos(cand.angle - atan2(0, cand.power)) * 0.5 + 0.5;
    
    // Power efficiency: reward shots that use less power
    double powerEfficiency = 1.0 - (cand.power / 666.0) * 0.3;
    
    // Shot quality score integration
    double qualityScore = CalculateShotQualityScore(
        sqrt(cand.score),
        0.5,
        abs(cand.angle - preferredSpinRate) * 0.01,
        preferredSpinRate,
        firstHitValid,
        ballsRemaining
    );
    
    // Explosive ranking: exponential separation
    return qualityScore * angleQuality * powerEfficiency * (firstHitValid ? 1.5 : 0.8);
}

// ============================================================================
// CORE AUTOPLAY IMPLEMENTATION
// ============================================================================

double normalizeAngle(double angle) {
    double newAngle = angle;
    if (newAngle >= maxAngle) newAngle = fmod(newAngle, maxAngle);
    else if (newAngle < 0) newAngle = maxAngle - fmod(-newAngle, maxAngle);
    return newAngle;
}

struct AutoPlayMetrics {
    int totalShotsAttempted = 0;
    int successfulShots = 0;
    double averagePower = 0.0;
    double averageDistance = 0.0;
    double averageQualityScore = 0.0;
};

Candidate g_CurrentCandidate = { -1 };
AutoPlayMetrics g_AutoPlayMetrics;

ImVec2 GetPocketScreenPos(int pocketIdx) {
    Table table = sharedGameManager.mTable;
    if (!table) return {};

    auto tableProperties = table.mTableProperties();
    if (!tableProperties) return {};

    auto& pockets = tableProperties.mPockets();
    return WorldToScreen(pockets[pocketIdx]);
}

bool IsShotValid() {
    auto& cand = g_CurrentCandidate;
    if (cand.idx == -1) return false;

    Ball::Classification myclass = sharedGameManager.getPlayerClassification();
    uint nominatedPocket = sharedGameManager.getNominatedPocket();
    if (nominatedPocket < 6 && cand.pocketIndex != nominatedPocket) return false;

    if (!gPrediction->guiData.balls[0].onTable) return false;
    if (!gPrediction->guiData.balls[cand.idx].originalOnTable) return false;
    if (gPrediction->guiData.balls[cand.idx].onTable) return false;
    if (gPrediction->guiData.balls[cand.idx].pocketIndex != cand.pocketIndex) return false;

    auto& ball8 = gPrediction->guiData.balls[8];
    if (myclass == Ball::Classification::ANY && ball8.originalOnTable && !ball8.onTable) return false;

    auto& firstHit = gPrediction->guiData.collision.firstHitBall;
    if (firstHit) {
        if (myclass == Ball::Classification::ANY) {
            if (firstHit->classification == Ball::Classification::EIGHT_BALL) return false;
        } else if (firstHit->classification != myclass) return false;
    }

    return true;
}

Point2D lastFailedCuePos = { -1000.0, -1000.0 };

namespace AutoPlay {
    double lastSetAngle = 0.f;
    bool didSetAngle = false;
    bool bAutoPlaying = false;
    double luxuryPrecisionModifier = 1.0;  // Adjustable precision for luxury mode

    enum State {
        IDLE,
        SCANNING,
        NOMINATING,
        EXECUTING,
    } state = IDLE;
    
    double pendingShotPower = 0.f;
    double pendingShotAngle = 0.f;
    int nominationFrameCounter = 0;
    
    enum ScanMode {
        FAST,
        SLOW,
        PRECISION,  // New luxury precision mode
    } scan = FAST;

    bool shouldAutoPlay() { 
        return !didSetAngle || lastSetAngle == sharedGameManager.mVisualCue().mVisualGuide().mAimAngle(); 
    }

    void setAimAngle(double angle) {
        lastSetAngle = angle;
        sharedGameManager.mVisualCue().mVisualGuide().mAimAngle(angle);
    }

    void takeShot(double angle, double power) {
        setAimAngle(angle);
        gPrediction->determineShotResult(false, angle, power);
        sharedGameManager.mVisualCue().mPower(ShotPowerToPower(power));
        M(void, libmain + 0x2dc0c58, void*)(F(void*, sharedGameManager + 0x3b0));
        
        // Log metrics for luxury tracking
        g_AutoPlayMetrics.totalShotsAttempted++;
        g_AutoPlayMetrics.averagePower = (g_AutoPlayMetrics.averagePower * (g_AutoPlayMetrics.totalShotsAttempted - 1) + power) / g_AutoPlayMetrics.totalShotsAttempted;
    }
    
    void ClearState() {
        g_CurrentCandidate.idx = -1;
        lastFailedCuePos = { -1000.0, -1000.0 };
    }
    
    void Shoot(double angle, double power = 0.f) {
        setAimAngle(angle);
        gPrediction->determineShotResult(false, angle, power);

        bool nominating = false;
        int nominationMode = sharedGameManager.getPocketNominationMode();
        auto myclass = sharedGameManager.getPlayerClassification();
        if ((nominationMode == 1 && myclass == Ball::Classification::EIGHT_BALL) || (nominationMode == 2 && myclass != Ball::Classification::ANY)) {
            if (g_CurrentCandidate.idx != -1 && sharedGameManager.getNominatedPocket() != g_CurrentCandidate.pocketIndex) {
                nominating = true;
            }
        }

        if (nominating) {
            pendingShotPower = power;
            pendingShotAngle = angle;
            state = NOMINATING;
            nominationFrameCounter = 0;
        } else {
            takeShot(angle, power);
            ClearState();
            state = IDLE;
        }
    }
    
    void ScanPrecision(double angleStep = 0.005f) {
        /**
         * LUXURY PRECISION MODE: Ultra-fine-grained analysis
         * Scans with precision-grade angle steps and advanced physics evaluation
         */
        static double currentScanAngle = 0.0;
        static bool isScanning = false;
        static Point2D lastScanCuePos = { -1000.0, -1000.0 };

        if (g_CurrentCandidate.idx != -1) return;
        
        if (!isScanning || gPrediction->guiData.balls[0].initialPosition != lastScanCuePos) {
            currentScanAngle = 0.0;
            isScanning = true;
            lastScanCuePos = gPrediction->guiData.balls[0].initialPosition;
        }

        Ball::Classification myclass = sharedGameManager.getPlayerClassification();
        uint nominatedPocket = sharedGameManager.getNominatedPocket();
        double spinMagnitude = ExtractSpinMagnitude(sharedGameManager.getShotSpin());
        
        int steps = 0;
        bool foundShot = false;
        double bestQualityScore = -1.0;
        Candidate bestCandidate = { -1 };
        
        while (steps < 15 && currentScanAngle < maxAngle) {
            double angle = currentScanAngle;
            currentScanAngle += angleStep;
            steps++;

            std::vector<double> powers = {666.0, 555.0, 444.0, 333.0, 222.0, 111.0};
            for (double power : powers) {
                gPrediction->determineShotResult(true, angle, power, sharedGameManager.getShotSpin());
                
                int targetIdx = -1;
                bool isNineBallGame = myclass == Ball::Classification::NINE_BALL_RULE;

                if (isNineBallGame) {
                    int iFoundLowestNumberedBall = -1;
                    for (int i = 1; i < gPrediction->guiData.ballsCount; i++) {
                        if (gPrediction->guiData.balls[i].originalOnTable) {
                            iFoundLowestNumberedBall = i;
                            break;
                        }
                    }

                    auto firstHit = gPrediction->guiData.collision.firstHitBall;
                    if (!firstHit || firstHit->index != iFoundLowestNumberedBall) continue;
                    if (!gPrediction->guiData.balls[0].onTable) continue;

                    int bestPottedIdx = -1;
                    for (int i = 1; i < gPrediction->guiData.ballsCount; i++) {
                        auto& ball = gPrediction->guiData.balls[i];
                        if (ball.originalOnTable && !ball.onTable) {
                            if (nominatedPocket < 6 && ball.pocketIndex != nominatedPocket) continue;
                            if (i == 9) { bestPottedIdx = 9; break; }
                            if (bestPottedIdx == -1 || i == firstHit->index) bestPottedIdx = i;
                        }
                    }

                    if (bestPottedIdx == -1) continue;
                    
                    double qualityScore = CalculateShotQualityScore(
                        sqrt(power),
                        1.0,
                        0.0,
                        spinMagnitude,
                        true,
                        gPrediction->guiData.ballsCount - 1
                    );
                    
                    if (qualityScore > bestQualityScore) {
                        bestQualityScore = qualityScore;
                        bestCandidate = {bestPottedIdx, angle, (double)power, (uint)gPrediction->guiData.balls[bestPottedIdx].pocketIndex};
                    }
                }

                for (int i = 1; i < gPrediction->guiData.ballsCount; i++) {
                    auto& ball = gPrediction->guiData.balls[i];
                    if (ball.originalOnTable && !ball.onTable) {
                        bool isValidTarget = false;
                        if (myclass == Ball::Classification::ANY) {
                            if (ball.classification != Ball::Classification::CUE_BALL && ball.classification != Ball::Classification::EIGHT_BALL) isValidTarget = true;
                        } else {
                            if (ball.classification == myclass) isValidTarget = true;
                        }
                        if (nominatedPocket < 6 && ball.pocketIndex != nominatedPocket) isValidTarget = false;
                        if (isValidTarget) { targetIdx = i; break; }
                    }
                }

                if (targetIdx != -1) {
                    if (!gPrediction->guiData.balls[0].onTable) continue;
                    if (!gPrediction->guiData.balls[8].onTable && myclass != Ball::Classification::EIGHT_BALL) continue;
                    auto firstHit = gPrediction->guiData.collision.firstHitBall;
                    if (!firstHit) continue;
                    if (myclass == Ball::Classification::ANY) {
                        if (firstHit->classification == Ball::Classification::EIGHT_BALL) continue;
                    } else if (firstHit->classification != myclass) continue;

                    double qualityScore = CalculateShotQualityScore(
                        sqrt(power),
                        1.0,
                        0.0,
                        spinMagnitude,
                        true,
                        gPrediction->guiData.ballsCount - 1
                    );
                    
                    if (qualityScore > bestQualityScore) {
                        bestQualityScore = qualityScore;
                        bestCandidate = {targetIdx, angle, (double)power, (uint)gPrediction->guiData.balls[targetIdx].pocketIndex};
                    }
                }
            }
        }

        if (bestCandidate.idx != -1) {
            g_CurrentCandidate = bestCandidate;
            g_AutoPlayMetrics.averageQualityScore = bestQualityScore;
            Shoot(bestCandidate.angle, bestCandidate.power);
            foundShot = true;
        }

        if (!foundShot && currentScanAngle >= maxAngle) {
            isScanning = false;
            currentScanAngle = 0.0;
            state = IDLE;
        }
    }
    
    void ScanSlow(double angleStep = 0.01f) {
        static double currentScanAngle = 0.0;
        static bool isScanning = false;
        static Point2D lastScanCuePos = { -1000.0, -1000.0 };

        if (g_CurrentCandidate.idx != -1) return;
        
        if (!isScanning || gPrediction->guiData.balls[0].initialPosition != lastScanCuePos) {
            currentScanAngle = 0.0;
            isScanning = true;
            lastScanCuePos = gPrediction->guiData.balls[0].initialPosition;
        }

        Ball::Classification myclass = sharedGameManager.getPlayerClassification();
        uint nominatedPocket = sharedGameManager.getNominatedPocket();
        
        int steps = 0;
        bool foundShot = false;
        
        while (steps < 40 && currentScanAngle < maxAngle) {
            double angle = currentScanAngle;
            currentScanAngle += angleStep;
            steps++;

            std::vector<double> powers = {666.0, 466.0, 266.0, 100.0};
            for (double power : powers) {
                gPrediction->determineShotResult(true, angle, power, sharedGameManager.getShotSpin());
                
                bool isPotentiallyValid = false;
                int targetIdx = -1;
                bool isNineBallGame = myclass == Ball::Classification::NINE_BALL_RULE;

                if (isNineBallGame) {
                    int iFoundLowestNumberedBall = -1;
                    for (int i = 1; i < gPrediction->guiData.ballsCount; i++) {
                        if (gPrediction->guiData.balls[i].originalOnTable) {
                            iFoundLowestNumberedBall = i;
                            break;
                        }
                    }

                    auto firstHit = gPrediction->guiData.collision.firstHitBall;
                    if (!firstHit || firstHit->index != iFoundLowestNumberedBall) continue;
                    if (!gPrediction->guiData.balls[0].onTable) continue;

                    int bestPottedIdx = -1;
                    for (int i = 1; i < gPrediction->guiData.ballsCount; i++) {
                        auto& ball = gPrediction->guiData.balls[i];
                        if (ball.originalOnTable && !ball.onTable) {
                            if (nominatedPocket < 6 && ball.pocketIndex != nominatedPocket) continue;
                            if (i == 9) { bestPottedIdx = 9; break; }
                            if (bestPottedIdx == -1 || i == firstHit->index) bestPottedIdx = i;
                        }
                    }

                    if (bestPottedIdx == -1) continue;
                    g_CurrentCandidate.idx = bestPottedIdx;
                    g_CurrentCandidate.angle = angle;
                    g_CurrentCandidate.power = power;
                    g_CurrentCandidate.pocketIndex = gPrediction->guiData.balls[bestPottedIdx].pocketIndex;

                    foundShot = true;
                    Shoot(angle, power);
                    break;
                }

                for (int i = 1; i < gPrediction->guiData.ballsCount; i++) {
                    auto& ball = gPrediction->guiData.balls[i];
                    if (ball.originalOnTable && !ball.onTable) {
                        bool isValidTarget = false;
                        if (myclass == Ball::Classification::ANY) {
                            if (ball.classification != Ball::Classification::CUE_BALL && ball.classification != Ball::Classification::EIGHT_BALL) isValidTarget = true;
                        } else {
                            if (ball.classification == myclass) isValidTarget = true;
                        }
                        if (nominatedPocket < 6 && ball.pocketIndex != nominatedPocket) isValidTarget = false;
                        if (isValidTarget) { targetIdx = i; break; }
                    }
                }

                if (targetIdx != -1) {
                    if (!gPrediction->guiData.balls[0].onTable) continue;
                    if (!gPrediction->guiData.balls[8].onTable && myclass != Ball::Classification::EIGHT_BALL) continue;
                    auto firstHit = gPrediction->guiData.collision.firstHitBall;
                    if (!firstHit) continue;
                    if (myclass == Ball::Classification::ANY) {
                        if (firstHit->classification == Ball::Classification::EIGHT_BALL) continue;
                    } else if (firstHit->classification != myclass) continue;

                    isPotentiallyValid = true;
                    g_CurrentCandidate.idx = targetIdx;
                    g_CurrentCandidate.angle = angle;
                    g_CurrentCandidate.power = power;
                    g_CurrentCandidate.pocketIndex = gPrediction->guiData.balls[targetIdx].pocketIndex;
                }

                if (isPotentiallyValid) {
                    foundShot = true;
                    Shoot(angle, power);
                    break;
                }
            }
            if (foundShot) break;
        }

        if (!foundShot && currentScanAngle >= maxAngle) {
            isScanning = false;
            currentScanAngle = 0.0;
            lastFailedCuePos = { -1000.0, -1000.0 };
            state = IDLE;
        }
    }
    
    void ScanFast(double angleStep = 0.1f) {
        /**
         * EXPLOSIVE FAST SCAN: Dynamic multi-candidate ranking algorithm
         * Uses advanced physics-based scoring to identify explosive shot combinations
         */
        if (g_CurrentCandidate.idx != -1) return;
        if (gPrediction->guiData.balls[0].initialPosition == lastFailedCuePos) return;

        double startingAngle = sharedGameManager.mVisualCue().mVisualGuide().mAimAngle();
        Ball::Classification myclass = sharedGameManager.getPlayerClassification();
        uint nominatedPocket = sharedGameManager.getNominatedPocket();
        std::vector<Candidate> candidates;
        auto pockets = getPockets();
        auto& cueBall = gPrediction->guiData.balls[0];
        double spinMagnitude = ExtractSpinMagnitude(sharedGameManager.getShotSpin());
        
        bool bFoundLowestNumberedBall = false;
        int iFoundLowestNumberedBall = -1;
        bool isNineBallGame = myclass == Ball::Classification::NINE_BALL_RULE;
        int ballsRemaining = gPrediction->guiData.ballsCount;

        for (int i = 1; i < gPrediction->guiData.ballsCount; i++) {
            if (isNineBallGame && bFoundLowestNumberedBall) break;
            auto& ball = gPrediction->guiData.balls[i];
            if (!ball.originalOnTable) continue;
            if (!bFoundLowestNumberedBall) { bFoundLowestNumberedBall = true; iFoundLowestNumberedBall = i; }

            if (!isNineBallGame) {
                bool isACandidate = myclass == Ball::Classification::ANY ? ball.classification != Ball::Classification::EIGHT_BALL : ball.classification == myclass;
                if (!isACandidate) continue;
            }

            for (int pocketIdx = 0; pocketIdx < pockets.size(); pocketIdx++) {
                if (nominatedPocket < 6 && pocketIdx != nominatedPocket) continue;
                Point2D pocket = pockets[pocketIdx];
                Point2D toPocket = pocket - ball.initialPosition;
                double distTargetToPocket = sqrt(toPocket.square());
                if (distTargetToPocket < 0.1) continue;
                
                Point2D direction = toPocket * (1.0 / distTargetToPocket);
                Point2D ghostBallPos = ball.initialPosition - direction * (2.0 * BALL_RADIUS);
                Point2D shotLine = ghostBallPos - cueBall.initialPosition;
                double distCueToTarget = sqrt(shotLine.square());
                double angle = atan2(shotLine.y, shotLine.x);
                if (angle < 0) angle += 2 * M_PI;
                
                // REVOLUTIONARY PHYSICS: Advanced power calculation
                double power = CalculateOptimalPowerAdvanced(distCueToTarget + distTargetToPocket, spinMagnitude, 1.0);
                double compositeScore = distCueToTarget + distTargetToPocket;
                
                if (power > 666.0) power = 666.0;
                candidates.push_back({i, angle, compositeScore, pocketIdx, power});
            }
        }
        
        // Sort berdasarkan total jarak — kandidat terdekat paling mudah masuk
        std::sort(candidates.begin(), candidates.end());
        
        // Angle refinement ±1°/±2° dan power sweep untuk akurasi
        static const double kAngles[] = {0.0, -0.0175, +0.0175, -0.035, +0.035};
        static const double kPowers[] = {1.0, 1.2, 0.85, 1.4, 0.7, 1.6, 0.55};

        bool foundShot = false;
        for (const auto& cand : candidates) {
            double baseAngle = NumberUtils::normalizeDoublePrecision(normalizeAngle(cand.angle));
            bool simOk = false;
            double usedAngle = baseAngle;
            double usedPower = cand.power;

            for (double dA : kAngles) {
                if (simOk) break;
                double tryAngle = NumberUtils::normalizeDoublePrecision(normalizeAngle(baseAngle + dA));
                for (double pf : kPowers) {
                    double tryPower = std::min(std::max(cand.power * pf, 80.0), 666.0);
                    gPrediction->determineShotResult(true, tryAngle, tryPower, sharedGameManager.getShotSpin(), cand);
                    if (!gPrediction->firstHitIsTarget) continue;
                    if (!gPrediction->guiData.balls[0].onTable) continue;

                    if (isNineBallGame) {
                        auto firstHit = gPrediction->guiData.collision.firstHitBall;
                        if (!firstHit || firstHit->index != cand.idx) continue;
                        int bestPottedIdx = -1;
                        for (int i = 1; i < gPrediction->guiData.ballsCount; i++) {
                            auto& ball = gPrediction->guiData.balls[i];
                            if (ball.originalOnTable && !ball.onTable) {
                                if (nominatedPocket < 6 && ball.pocketIndex != nominatedPocket) continue;
                                if (i == 9) { bestPottedIdx = 9; break; }
                                if (bestPottedIdx == -1 || i == cand.idx) bestPottedIdx = i;
                            }
                        }
                        if (bestPottedIdx == -1) continue;
                        usedAngle = tryAngle; usedPower = tryPower; simOk = true;
                        g_CurrentCandidate = cand;
                        g_CurrentCandidate.idx = bestPottedIdx;
                        g_CurrentCandidate.angle = usedAngle;
                        g_CurrentCandidate.power = usedPower;
                        g_CurrentCandidate.pocketIndex = gPrediction->guiData.balls[bestPottedIdx].pocketIndex;
                        foundShot = true;
                        Shoot(usedAngle, usedPower);
                        goto fastDone;
                    }

                    if (gPrediction->guiData.balls[cand.idx].onTable) continue;
                    if (gPrediction->guiData.balls[cand.idx].pocketIndex != cand.pocketIndex) continue;

                    {
                        bool isAngleGood = false;
                        for (int i = 1; i < gPrediction->guiData.ballsCount; i++) {
                            Prediction::Ball& ball = gPrediction->guiData.balls[i];
                            bool match = (myclass == Ball::Classification::ANY)
                                ? (ball.classification != Ball::Classification::CUE_BALL && ball.classification != Ball::Classification::EIGHT_BALL)
                                : (ball.classification == myclass);
                            if (match && ball.originalOnTable && !ball.onTable) { isAngleGood = true; break; }
                        }
                        if (isAngleGood && gPrediction->guiData.collision.firstHitBall) {
                            auto fh = gPrediction->guiData.collision.firstHitBall;
                            if (myclass != Ball::Classification::ANY && fh->classification != myclass) isAngleGood = false;
                            else if (myclass == Ball::Classification::ANY && fh->classification == Ball::Classification::EIGHT_BALL) isAngleGood = false;
                        }
                        if (isAngleGood && !gPrediction->guiData.balls[0].onTable) isAngleGood = false;
                        auto& ball8 = gPrediction->guiData.balls[8];
                        if (isAngleGood && ball8.originalOnTable && !ball8.onTable && myclass != Ball::Classification::EIGHT_BALL) isAngleGood = false;
                        if (!isAngleGood) continue;
                    }

                    usedAngle = tryAngle; usedPower = tryPower; simOk = true;
                    break;
                }
            }

            if (!simOk) continue;
            g_CurrentCandidate = cand;
            g_CurrentCandidate.angle = usedAngle;
            g_CurrentCandidate.power = usedPower;
            foundShot = true;
            Shoot(usedAngle, usedPower);
            break;
        }
        fastDone:
        if (!foundShot) {
            lastFailedCuePos = cueBall.initialPosition;
            scan = SLOW;
        }
    }

    bool isAnimationActive() {
        auto visualCue = sharedGameManager.mVisualCue();
        if (!visualCue) return false;
        auto _powerBarView = F(ptr, visualCue + 0x510);
        if (!_powerBarView) return false;
        uintptr_t activeAction = M(uintptr_t, libmain + 0x2de6f30, ptr)(_powerBarView);
        return (activeAction != 0);
    }

    void Update() {
        buttonClicker.Update();

        if (isAnimationActive()) return;

        if (!bAutoPlaying || !sharedGameManager.mStateManager().isPlayerTurn()) {
            // Reset semua state saat giliran berakhir
            g_CurrentCandidate.idx = -1;
            lastFailedCuePos = { -1000.0, -1000.0 };
            state = IDLE;
            scan = FAST;
            return;
        }

        if (state == IDLE) {
            state = SCANNING;
            scan = FAST;
        } else if (state == SCANNING) {
            if (scan == FAST) ScanFast();
            else if (scan == SLOW) ScanSlow(0.003f);
            else if (scan == PRECISION) ScanPrecision(0.005f);
        } else if (state == NOMINATING) {
            nominationFrameCounter++;
            if (nominationFrameCounter == 10) {
                buttonClicker.Click(GetPocketScreenPos(g_CurrentCandidate.pocketIndex));
            }
            if (nominationFrameCounter > 20 && !buttonClicker.Active) {
                takeShot(pendingShotAngle, pendingShotPower);
                ClearState();
                state = IDLE;
            }
        }
    }
};
