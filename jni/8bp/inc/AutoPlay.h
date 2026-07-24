#pragma once

#include "Prediction.fast.h"
#include <imgui/imgui.h>
#include <algorithm>
#include <cmath>
#include <vector>

#include "ScreenTable.h"
#include "PhysicsModel.h"
#include "GameSpeedControl.h"
//#include "8bp/FrictionProperties.h"
#include "ButtonClicker.h"

using namespace ImGui;

// ============================================================================
// CONSTANTS & CONFIGURATION
// ============================================================================
#ifndef PI
#define PI 3.14159265358979323846
#endif

const double TWO_PI = 2.0 * PI;
const double ANGLE_STEP_FAST = 0.05;      // 0.05 radians (~2.86 degrees)
const double ANGLE_STEP_SLOW = 0.02;      // 0.02 radians (~1.15 degrees)
const double MIN_POCKET_DIST = 40.0;      // Minimum distance to pocket
const double MAX_POCKET_DIST = 120.0;     // Maximum distance to pocket
const double BALL_SAFETY_MARGIN = 5.0;    // Safety margin around ball

// Ball type classifications
enum BallType {
    CUE_BALL = 0,
    SOLIDS = 1,      // 1-7
    STRIPES = 2,     // 9-15
    EIGHT_BALL = 3,
    INVALID = -1
};

constexpr double maxAngle = 360.0 / (180.0 / M_PI);

double normalizeAngle(double angle) {
    double newAngle = angle;
    if (newAngle >= maxAngle) newAngle = fmod(newAngle, maxAngle);
    else if (newAngle < 0) newAngle = maxAngle - fmod(-newAngle, maxAngle);
    return newAngle;
}

Candidate g_CurrentCandidate = { -1 };
extern void DrawEightBallLoading(ImDrawList*);

ImVec2 GetPocketScreenPos(int pocketIdx) {
    Table table = sharedGameManager.mTable;
    if (!table) return {};
    auto tableProperties = table.mTableProperties();
    if (!tableProperties) return {};
    auto& pockets = tableProperties.mPockets();
    return WorldToScreen(pockets[pocketIdx]);
}

static BallType getBallType(int ballIndex) {
    if (ballIndex == 0) return CUE_BALL;
    if (ballIndex == 8) return EIGHT_BALL;
    if (ballIndex >= 1 && ballIndex <= 7) return SOLIDS;
    if (ballIndex >= 9 && ballIndex <= 15) return STRIPES;
    return INVALID;
}

// ============================================================================
// PHYSICS ENGINE - CORRECTED FOR PROPER BALL COLLISION
// ============================================================================
struct PhysicsEngine {
    static constexpr double BALL_DIAMETER = 2.0 * Physics::BALL_RADIUS;
    static constexpr double GRAVITY = 9.81;
    
    // ========================================================================
    // EQUATION 1: Calculate cue power to transfer momentum to target ball
    // Physics: Elastic collision - cue ball transfers energy to target
    // v_target = (2 * m_cue / (m_cue + m_target)) * v_cue
    // Simplified: v_target = v_cue (equal mass)
    // Required: Power = Distance * Friction_Coefficient
    // ========================================================================
    static double calculatePowerForTargetToPocket(
        double cueToBallDist,
        double ballToPocketDist,
        const FrictionProperties& friction
    ) {
        if (ballToPocketDist < 1.0) return 100.0;

        // Pakai sliding factor (196) bukan rolling factor (0.0111) yang terlalu kecil
        double deceleration = friction._velocityReductionSlidingFactor > 1.0
            ? friction._velocityReductionSlidingFactor
            : 196.0; // fallback ke nilai default

        // v = sqrt(2 * a * s) untuk target ball sampai ke pocket
        double requiredVelocity = std::sqrt(2.0 * deceleration * ballToPocketDist);

        // Tambah energi buat kompensasi loss di collision (~10%) dan jarak cue ke bola
        double collisionOverhead = requiredVelocity / 0.9;
        double cueOverhead       = std::sqrt(2.0 * deceleration * cueToBallDist) * 0.4;
        double power             = collisionOverhead + cueOverhead;

        return std::min(std::max(power, 80.0), 666.0);
    }
    
    // ========================================================================
    // GHOST BALL POSITION: where the CUE BALL's center must be at the moment
    // of contact so the target ball travels toward the pocket. This is
    // BALL_DIAMETER (2 * radius) away from the target ball's center, along
    // the line from the pocket through the target ball.
    // ========================================================================
    static Point2D calculateGhostBallPosition(
        const Point2D& targetBallPos,
        const Point2D& pocketPos
    ) {
        Point2D toPocket = pocketPos - targetBallPos;
        double distance = std::sqrt(toPocket.square());
        if (distance < 0.1) return targetBallPos;

        Point2D direction = toPocket * (1.0 / distance);
        return targetBallPos - direction * BALL_DIAMETER;
    }

    // ========================================================================
    // EQUATION 2: Calculate the AIM ANGLE for the cue ball.
    //
    // FIX: previously this returned atan2(pocket - targetBall) — the
    // direction the TARGET ball needs to travel, completely ignoring where
    // the CUE BALL actually is. That's only correct by coincidence (cue,
    // target, and pocket happen to be collinear). The real aim angle is the
    // direction from the CUE BALL to the GHOST BALL position.
    // ========================================================================
    static double calculateAngleToPocket(
        const Point2D& cueBallPos,
        const Point2D& targetBallPos,
        const Point2D& pocketPos
    ) {
        Point2D ghostBallPos = calculateGhostBallPosition(targetBallPos, pocketPos);

        Point2D cueToGhost = ghostBallPos - cueBallPos;
        double angle = std::atan2(cueToGhost.y, cueToGhost.x);

        if (angle < 0) angle += TWO_PI;
        return angle;
    }
    
    // ========================================================================
    // EQUATION 3: Collision point == ghost ball position (kept for backward
    // compatibility with call sites that use it for distance calculations).
    // FIX: now uses BALL_DIAMETER via calculateGhostBallPosition (was
    // BALL_RADIUS — half the correct distance).
    // ========================================================================
    static Point2D calculateCollisionPoint(
        const Point2D& targetBallPos,
        const Point2D& pocketPos
    ) {
        return calculateGhostBallPosition(targetBallPos, pocketPos);
    }
    
    // ========================================================================
    // EQUATION 4: Calculate shot accuracy (how direct is the path?)
    // Physics: Accuracy = alignment between target->pocket and cue->target
    // Direct path = 1.0, off-angle = 0.0
    // ========================================================================
    static double calculateShotAccuracy(
        const Point2D& cueBallPos,
        const Point2D& targetBallPos,
        const Point2D& pocketPos
    ) {
        // Vector from cue to target
        Point2D cueToTarget = targetBallPos - cueBallPos;
        // Vector from target to pocket
        Point2D targetToPocket = pocketPos - targetBallPos;
        
        double cueToTargetLen = std::sqrt(cueToTarget.square());
        double targetToPocketLen = std::sqrt(targetToPocket.square());
        
        if (cueToTargetLen < 0.1 || targetToPocketLen < 0.1) return 0.0;
        
        // Dot product: measures alignment
        double dotProduct = (cueToTarget.x * targetToPocket.x) + 
                           (cueToTarget.y * targetToPocket.y);
        
        // Normalize to [0, 1]
        double accuracy = dotProduct / (cueToTargetLen * targetToPocketLen);
        
        return std::max(0.0, std::min(1.0, accuracy));
    }
    
    // ========================================================================
    // EQUATION 5: Calculate shot score (lower = better)
    // Score: (distance_to_pocket) * (1 - accuracy) * ball_priority
    // ========================================================================
    static double calculateShotScore(
        double targetBallToPocketDist,  // ACTUAL distance ball travels
        double accuracy,
        BallType ballType,
        BallType myBallType,
        bool isMyBall
    ) {
        // Base: favor close pockets
        double baseScore = targetBallToPocketDist;
        
        // Reward accuracy (direct shots are better)
        baseScore *= (1.0 - (accuracy * 0.5));  // High accuracy = lower score
        
        // Ball type priority
        if (isMyBall) {
            // MY BALLS: 0.2x multiplier (STRONG PRIORITY - chosen first)
            baseScore *= 0.2;
        } else if (ballType != EIGHT_BALL) {
            // OPPONENT BALLS: 3.0x multiplier (LOW PRIORITY - fallback only)
            baseScore *= 3.0;
        }
        
        return baseScore;
    }
    
    // ========================================================================
    // Validate pocket is reachable from target ball
    // ========================================================================
    static bool isPocketReachable(double targetBallToPocketDist) {
        return targetBallToPocketDist > 1.0; // hanya skip kalau bola udah di dalam pocket
    }
    
    // ========================================================================
    // Validate cue ball won't scratch (won't be potted)
    // ========================================================================
    static bool validateCueBallSafety(const Prediction& pred) {
        return pred.guiData.balls[0].onTable;  // Cue ball still on table
    }
    
    // ========================================================================
    // Validate 8-ball won't be potted prematurely
    // ========================================================================
    static bool validateEightBallSafety(const Prediction& pred, BallType myBallType) {
        auto& ball8 = pred.guiData.balls[8];
        
        // 8-ball must stay on table until it's your turn (you've cleared all yours)
        if (ball8.originalOnTable && !ball8.onTable && myBallType != EIGHT_BALL) {
            return false;  // 8-ball was knocked in prematurely!
        }
        
        return true;
    }
    
    // ========================================================================
    // Validate first ball hit matches player's ball type
    // ========================================================================
    static bool validateFirstHit(const Prediction& pred, BallType myBallType, BallType targetBallType) {
        auto firstHit = pred.guiData.collision.firstHitBall;
        if (!firstHit) return false;

        BallType hitType = INVALID;
        if (firstHit->index == 8 || firstHit->classification == Ball::Classification::EIGHT_BALL) {
            hitType = EIGHT_BALL;
        } else if (firstHit->index >= 1 && firstHit->index <= 7) {
            hitType = SOLIDS;
        } else if (firstHit->index >= 9 && firstHit->index <= 15) {
            hitType = STRIPES;
        }

        // Kalau giliran 8-ball, first hit harus bola 8
        if (myBallType == EIGHT_BALL) return hitType == EIGHT_BALL;

        // Kalau open table, boleh kena bola apapun kecuali bola 8
        if (myBallType == SOLIDS && targetBallType == STRIPES) {
            // open table — boleh kena solid atau stripe asal bukan 8
            return hitType != EIGHT_BALL && hitType != CUE_BALL;
        }

        // Normal: first hit HARUS bola kita sendiri
        return hitType == myBallType;
    }
    
    // ========================================================================
    // Validate target ball actually gets potted (not opponent ball)
    // ========================================================================
    static bool validateTargetBallPocketed(const Prediction& pred, int targetIdx, BallType myBallType) {
        auto& targetBall = pred.guiData.balls[targetIdx];
        // Target harus masuk pocket
        if (!targetBall.originalOnTable || targetBall.onTable) return false;

        // Pastikan tidak ada bola musuh yang ikut masuk
        for (int i = 1; i < pred.guiData.ballsCount; i++) {
            if (i == targetIdx) continue;
            auto& ball = pred.guiData.balls[i];
            if (!ball.originalOnTable || ball.onTable) continue; // masih di meja, aman

            BallType t = getBallType(i);
            // Bola musuh masuk = invalid
            if (myBallType == SOLIDS && t == STRIPES) return false;
            if (myBallType == STRIPES && t == SOLIDS) return false;
        }
        return true;
    }
};

// ============================================================================
// GAME STATE & HELPER FUNCTIONS
// ============================================================================
Point2D lastFailedCuePos = { -1000.0, -1000.0 };

BallType getPlayerBallType(Ball::Classification classification) {
    if (classification == Ball::Classification::STRIPE) return STRIPES;
    if (classification == Ball::Classification::EIGHT_BALL) return EIGHT_BALL;
    // SOLID atau ANY (open table) → default ke SOLIDS
    return SOLIDS;
}

// ============================================================================
// 9-BALL: find the lowest-numbered ball still on the table (1-9). This is
// the ONLY legal target — the cue ball must hit this ball first, and ANY
// ball pocketed is fine as long as this rule was followed.
// ============================================================================

// ============================================================================
// AUTOPLAY NAMESPACE
// ============================================================================
namespace AutoPlay {
    double lastSetAngle = 0.f;
    double lastSetPower = 0.f;
    bool bAutoPlaying = false;
    
    static FrictionProperties cachedFriction = {0.2, 0.0111, 0.025, 0.0014577259475218659, 196, 10.878, 9.8};

    enum State { IDLE, SCANNING, NOMINATING, EXECUTING } state = IDLE;
    enum ScanMode { FAST, SLOW } scan = FAST;
    
    double pendingShotPower = 0.f;
    double pendingShotAngle = 0.f;
    int nominationFrameCounter = 0;

    // ========================================================================
    // HELPER: Set aim angle
    // ========================================================================
    void setAimAngle(double angle) {
        lastSetAngle = angle;
        sharedGameManager.mVisualCue().mVisualGuide().mAimAngle(angle);
    }

    // ========================================================================
    // HELPER: Set shot power
    // ========================================================================
    void setShotPower(double power) {
        lastSetPower = power;
        sharedGameManager.mVisualCue().setShotPower(power);
    }

    // ========================================================================
    // HELPER: Execute shot
    // ========================================================================
    void takeShot(double angle, double power) {
        setAimAngle(angle);
        setShotPower(power);
        gPrediction->determineShotResult(false, angle, power);
        sharedGameManager.mVisualCue().mPower(ShotPowerToPower(power));
        M(void, libmain + 0x2dc0c58, void*)(F(void*, sharedGameManager + 0x3b0));
    }
    
    // ========================================================================
    // HELPER: Clear state
    // ========================================================================
    void ClearState() {
        g_CurrentCandidate.idx = -1;
        lastFailedCuePos = { -1000.0, -1000.0 };
    }
    
    // ========================================================================
    // MAIN: Execute shot with nomination
    // ========================================================================
    void Shoot(double angle, double power = 0.f) {
        setAimAngle(angle);
        setShotPower(power);
        gPrediction->determineShotResult(false, angle, power);

        bool nominating = false;
        int nominationMode = sharedGameManager.getPocketNominationMode();
        auto myclass = sharedGameManager.getPlayerClassification();
        
        if ((nominationMode == 1 && myclass == Ball::Classification::EIGHT_BALL) || 
            (nominationMode == 2 && myclass != Ball::Classification::ANY)) {
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
    
    // ========================================================================
    // SCAN FAST: Quick scan with physics-corrected angle calculation
    // ========================================================================
    void ScanFast(double angleStep = ANGLE_STEP_FAST) {
        if (g_CurrentCandidate.idx != -1) return;
        if (gPrediction->guiData.balls[0].initialPosition == lastFailedCuePos) return;

        Ball::Classification playerClass = sharedGameManager.getPlayerClassification();
        BallType myBallType = getPlayerBallType(playerClass);
        bool isOpenTable = (playerClass == Ball::Classification::ANY);
        uint nominatedPocket = sharedGameManager.getNominatedPocket();
        
        std::vector<Candidate> candidates;
        auto pockets = getPockets();
        auto& cueBall = gPrediction->guiData.balls[0];
        
        // ====================================================================
        // ITERATE: All balls on table
        // ====================================================================
        for (int i = 1; i < gPrediction->guiData.ballsCount; i++) {
            auto& ball = gPrediction->guiData.balls[i];
            if (!ball.originalOnTable) continue;
            
            BallType ballType = getBallType(i);
            bool isMyBall = (ballType == myBallType);  // hapus "&& ballType != EIGHT_BALL"

            bool isCandidate = false;
            if (isMyBall) {
                isCandidate = true;
            } else if (isOpenTable && ballType != EIGHT_BALL && ballType != CUE_BALL) {
                isCandidate = true;
            }
            
            if (!isCandidate) continue;
            // ================================================================
            // ITERATE: All pockets
            // ================================================================
            for (int pocketIdx = 0; pocketIdx < pockets.size(); pocketIdx++) {
                if (nominatedPocket < 6 && pocketIdx != nominatedPocket) continue;

                Point2D pocket = pockets[pocketIdx];
                
                // CORRECTED: Distance from TARGET BALL to POCKET (not cue to target)
                Point2D ballToPocket = pocket - ball.initialPosition;
                double ballToPocketDist = std::sqrt(ballToPocket.square());
                
                if (!PhysicsEngine::isPocketReachable(ballToPocketDist)) continue;
                
                // CORRECTED: Calculate where cue should hit the target ball
                // So that target ball goes DIRECTLY toward pocket
                Point2D collisionPoint = PhysicsEngine::calculateCollisionPoint(
                    ball.initialPosition,
                    pocket
                );
                
                // Calculate angle from cue to collision point
                Point2D cueToCollision = collisionPoint - cueBall.initialPosition;
                double cueToCollisionDist = std::sqrt(cueToCollision.square());
                if (cueToCollisionDist < 0.1) continue;
                
                double angle = PhysicsEngine::calculateAngleToPocket(
                    cueBall.initialPosition,
                    ball.initialPosition,
                    pocket
                );
                
                // Calculate accuracy
                double accuracy = PhysicsEngine::calculateShotAccuracy(
                    cueBall.initialPosition,
                    ball.initialPosition,
                    pocket
                );
                
                // Calculate score
                double score = PhysicsEngine::calculateShotScore(
                    ballToPocketDist,
                    accuracy,
                    ballType,
                    myBallType,
                    isMyBall
                );
                
                // CORRECTED: Calculate power needed for TARGET BALL to reach pocket
                double power = PhysicsEngine::calculatePowerForTargetToPocket(
                    cueToCollisionDist,
                    ballToPocketDist,
                    cachedFriction
                );
                
                candidates.push_back({i, angle, score, pocketIdx, power});
            }
        }
       std::sort(candidates.begin(), candidates.end());
        
        bool foundShot = false;
        
        for (const auto& cand : candidates) {
            // Coba angle persis + refinement kecil di sekitarnya
            constexpr double offsets[] = {0.0, 0.003, -0.003, 0.007, -0.007, 0.012, -0.012};
            bool candFound = false;

            for (double offset : offsets) {
                double angle = NumberUtils::normalizeDoublePrecision(normalizeAngle(cand.angle + offset));
                gPrediction->determineShotResult(true, angle, cand.power, sharedGameManager.getShotSpin(), cand);

                if (!PhysicsEngine::validateCueBallSafety(*gPrediction)) continue;
                if (!PhysicsEngine::validateEightBallSafety(*gPrediction, myBallType)) continue;
                if (!PhysicsEngine::validateFirstHit(*gPrediction, myBallType, getBallType(cand.idx))) continue;
                if (!PhysicsEngine::validateTargetBallPocketed(*gPrediction, cand.idx, myBallType)) continue;
                if (gPrediction->guiData.balls[cand.idx].pocketIndex != cand.pocketIndex) continue;

                LOGI("AutoPlay: FAST - Ball %d angle %f (offset %f) power %f", cand.idx, angle, offset, cand.power);
                g_CurrentCandidate = cand;
                g_CurrentCandidate.angle = angle;
                foundShot = candFound = true;
                Shoot(angle, cand.power);
                break;
            }
            if (candFound) break;
        }

        if (!foundShot) {
            lastFailedCuePos = cueBall.initialPosition;
            LOGI("AutoPlay: ScanFast failed, switching to ScanSlow");
            scan = SLOW;
        }
    }

    // ========================================================================
    // SCAN SLOW: Exhaustive angle search for any possible shot
    // ========================================================================
    void ScanSlow(double angleStep = ANGLE_STEP_SLOW) {
        static double currentScanAngle = 0.0;
        static bool isScanning = false;
        static Point2D lastScanCuePos = { -1000.0, -1000.0 };

        if (g_CurrentCandidate.idx != -1) return;
        
        if (!isScanning || gPrediction->guiData.balls[0].initialPosition != lastScanCuePos) {
            currentScanAngle = 0.0;
            isScanning = true;
            lastScanCuePos = gPrediction->guiData.balls[0].initialPosition;
        }

        Ball::Classification playerClass = sharedGameManager.getPlayerClassification();
        BallType myBallType = getPlayerBallType(playerClass);
        bool isOpenTable = (playerClass == Ball::Classification::ANY);
        uint nominatedPocket = sharedGameManager.getNominatedPocket();
        auto& cueBall = gPrediction->guiData.balls[0];
        
        int steps = 0;
        int stepsPerFrame = (int)(20 * GameSpeed::getAnimationMultiplier());
        
        // ====================================================================
        // ITERATE: All angles
        // ====================================================================
        while (steps < stepsPerFrame && currentScanAngle < maxAngle) {
            double angle = currentScanAngle;
            currentScanAngle += angleStep;
            steps++;

            // Strategic power levels for testing
           // std::vector<double> powers = {666.0, 500.0, 350.0, 200.0, 100.0};
            std::vector<double> powers = {666.0, 350.0};
            for (double power : powers) {
                gPrediction->determineShotResult(true, angle, power, sharedGameManager.getShotSpin());
                
                // Safety checks FIRST
                if (!PhysicsEngine::validateCueBallSafety(*gPrediction)) continue;
                if (!PhysicsEngine::validateEightBallSafety(*gPrediction, myBallType)) continue;
                if (!PhysicsEngine::validateFirstHit(*gPrediction, myBallType, myBallType)) continue;
                
                // Find what was potted — pastikan bola valid dan tidak ada bola musuh ikut masuk
                int targetIdx = -1;
                bool opponentBallPotted = false;
                for (int i = 1; i < gPrediction->guiData.ballsCount; i++) {
                    auto& ball = gPrediction->guiData.balls[i];
                    if (!ball.originalOnTable || ball.onTable) continue;

                    BallType ballType = getBallType(i);
                    bool isMyBall = (ballType == myBallType);
                    bool isOpponent = (!isOpenTable && !isMyBall && ballType != EIGHT_BALL && ballType != CUE_BALL);
                    if (isOpponent) { opponentBallPotted = true; break; }

                    bool isValid = isMyBall || (isOpenTable && ballType != EIGHT_BALL && ballType != CUE_BALL);
                    if (nominatedPocket < 6 && ball.pocketIndex != nominatedPocket) isValid = false;
                    if (isValid && targetIdx == -1) targetIdx = i;
                }
                if (opponentBallPotted) continue;
                if (targetIdx == -1) continue;

                LOGI("AutoPlay: SLOW - Ball %d angle %f power %f", targetIdx, angle, power);
                g_CurrentCandidate.idx = targetIdx;
                g_CurrentCandidate.angle = angle;
                g_CurrentCandidate.power = power;
                g_CurrentCandidate.pocketIndex = gPrediction->guiData.balls[targetIdx].pocketIndex;
                isScanning = false;
                currentScanAngle = 0.0;
                Shoot(angle, power);
                return;
            }
        }

        if (currentScanAngle >= maxAngle) {
            LOGI("AutoPlaySlow: Exhaustive scan complete, no shot found");
            isScanning = false;
            currentScanAngle = 0.0;
            state = IDLE;
        }
    }

    // ========================================================================
    // UI: Draw toggle button
    // ========================================================================
    void DrawToggleButton() {
        ImGuiIO& io = GetIO();
        float button_size = ImGui::GetFrameHeight() * 2.3f;
        float windowWidth = button_size + GetStyle().WindowPadding.x * 2;
        float windowHeight = button_size + GetStyle().WindowPadding.y * 2;

        SetNextWindowPos(ImVec2(io.DisplaySize.x - 155 - windowWidth, io.DisplaySize.y - 20 - windowHeight), ImGuiCond_Always);
        SetNextWindowSize(ImVec2(windowWidth, windowHeight), ImGuiCond_Always);
        
        PushStyleColor(ImGuiCol_WindowBg, IM_COL32(0, 0, 0, 0));
        PushStyleColor(ImGuiCol_Border, IM_COL32(0, 0, 0, 0));
        PushStyleVar(ImGuiStyleVar_WindowRounding, 5.0f);
        
        if (Begin("AutoPlay", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings)) {
            auto DrawPlayPauseButton = [&](bool isPause) -> bool {
                ImVec2 pos = GetCursorScreenPos();
                ImVec2 size(button_size, button_size);
                ImVec2 end(pos.x + size.x, pos.y + size.y);
                ImVec2 center(pos.x + size.x * 0.5f, pos.y + size.y * 0.5f);
                
                PushStyleColor(ImGuiCol_Button, IM_COL32(50, 50, 50, 180));
                PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(80, 80, 80, 200));
                PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(100, 100, 100, 200));
                PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 255, 255));
                
                bool clicked = Button("##AutoPlayBtn", size);
                
                ImDrawList* dl = GetWindowDrawList();
                float h = size.y * 0.4f;
                float w = h * 0.8f;

                if (isPause) {
                    float bar_w = w * 0.35f;
                    float gap = w * 0.3f;
                    dl->AddRectFilled(ImVec2(center.x - gap/2 - bar_w, center.y - h/2), ImVec2(center.x - gap/2, center.y + h/2), IM_COL32(255, 255, 255, 180));
                    dl->AddRectFilled(ImVec2(center.x + gap/2, center.y - h/2), ImVec2(center.x + gap/2 + bar_w, center.y + h/2), IM_COL32(255, 255, 255, 180));
                } else {
                    float off_x = h * 0.3f;
                    dl->AddTriangleFilled(ImVec2(center.x - off_x, center.y - h/2), ImVec2(center.x - off_x, center.y + h/2), ImVec2(center.x + off_x * 1.5f, center.y), IM_COL32(255, 255, 255, 180));
                }
                
                GetForegroundDrawList()->AddRect(pos, end, IM_COL32(200, 200, 200, 255), 5.0f, 0, 2.0f);
                PopStyleColor(4);
                return clicked;
            };

            if (DrawPlayPauseButton(bAutoPlaying)) {
                bAutoPlaying = !bAutoPlaying;
                if (bAutoPlaying) ClearState();
            }
        } 
        End();

        PopStyleVar();
        PopStyleColor(2);
    }

    // ========================================================================
    // CHECK: Is animation active?
    // ========================================================================
    bool isAnimationActive() {
        auto visualCue = sharedGameManager.mVisualCue();
        if (!visualCue) return true;
        
        auto _powerBarView = F(ptr, visualCue + 0x510);
        if (!_powerBarView) return true;

        auto activeAction = M(ptr, libmain + 0x2de6f30, ptr)(_powerBarView);
        return activeAction != 0;
    }
    
    // ========================================================================
    // MAIN: Update loop
    // ========================================================================
    void Update() {
        buttonClicker.Update();
        DrawToggleButton();

        if (isAnimationActive()) return;
        if (!bAutoPlaying || !sharedGameManager.mStateManager().isPlayerTurn()) {
            state = IDLE;
            return;
        }

        if (state == IDLE) {
            state = SCANNING;
            scan = FAST;
        } 
        if (state == SCANNING) {
            if (scan == FAST) {
                ScanFast(ANGLE_STEP_FAST);
            } else {
                DrawEightBallLoading(GetForegroundDrawList());
                ScanSlow(ANGLE_STEP_SLOW);
            }
        } 
        if (state == NOMINATING) {
            nominationFrameCounter++;
            if (nominationFrameCounter == 10) {
                buttonClicker.Click(GetPocketScreenPos(g_CurrentCandidate.pocketIndex));
            }
            if (nominationFrameCounter > 20 && !buttonClicker.Active) {
                takeShot(g_CurrentCandidate.angle, g_CurrentCandidate.power);
                ClearState();
                state = IDLE;
            }
        }
    }
};
