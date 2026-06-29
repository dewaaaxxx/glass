#pragma once

#include "Prediction.fast.h"
#include <imgui/imgui.h>
#include <algorithm>
#include <chrono>
#include <cmath>

#include "ScreenTable.h"
#include "PowerSlider.h"
#include "ButtonClicker.h"

using namespace ImGui;

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

bool IsShotValid() {
    auto& cand = g_CurrentCandidate;
    if (cand.idx == -1) return false;

    Ball::Classification myclass = sharedGameManager.getPlayerClassification();

    uint nominatedPocket = sharedGameManager.getNominatedPocket();
    if (nominatedPocket < 6 && cand.pocketIndex != nominatedPocket) return false;

    if (!gPrediction->guiData.balls[0].onTable) return false; // cue ball should not be pocketed
    if (!gPrediction->guiData.balls[cand.idx].originalOnTable) return false; // target ball was already potted
    if (gPrediction->guiData.balls[cand.idx].onTable) return false; // target ball was not potted
    if (gPrediction->guiData.balls[cand.idx].pocketIndex != cand.pocketIndex) return false; // target ball did not go into target pocket

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

/* void UpdatePowerSlider() {
    static bool isSearchingExtraPower = false;
    static float savedLowestPower = 0.0f;
    static ImVec2 savedLowestPos = ImVec2(0, 0);
    static ImVec2 bestValidPos = ImVec2(0, 0);

    if (powerSlider.state != powerSlider.State::MOVING) {
        isSearchingExtraPower = false;
        return;
    }

    float currentPower = sharedGameManager.mVisualCue().getShotPower(true);
    bool isValid = IsShotValid();

    if (!isSearchingExtraPower) {
        if (isValid && currentPower < 666.0f) {
            isSearchingExtraPower = true;
            savedLowestPower = currentPower;
            savedLowestPos = powerSlider.CurrentPos;
            bestValidPos = powerSlider.CurrentPos;
            LOGI("Found valid shot at %f, searching for more power...", currentPower);
        }
    } else {
        if (isValid) bestValidPos = powerSlider.CurrentPos;

        if (!isValid) {
            float midX = bestValidPos.x;// + (powerSlider.CurrentPos.x - savedLowestPos.x) * 0.5f;
            float midY = bestValidPos.y;// + (powerSlider.CurrentPos.y - savedLowestPos.y) * 0.5f;
            
            powerSlider.CurrentPos = ImVec2(midX, midY);
            NativeTouchesMove(powerSlider.TouchIndex, midX, midY);
            
            LOGI("Shot invalid at %f. Backing off and ending.", currentPower);
            powerSlider.state = powerSlider.State::ENDING;
            isSearchingExtraPower = false;
        } else if (currentPower >= savedLowestPower + 80.0f) {
            LOGI("Reached +80 power (%f -> %f). Forcefully ending.", savedLowestPower, currentPower);
            powerSlider.state = powerSlider.State::ENDING;
            isSearchingExtraPower = false;
        }
    }
} */

Point2D lastFailedCuePos = { -1000.0, -1000.0 };

// ── Drag animation state (diambil dari AutoPlay_impl.h) ──────────────────────
static double anim_TargetAngle  = 0.0;
static double anim_TargetPower  = 0.0;
static bool   anim_IsPulling    = false;
static bool   anim_RotationDone = false;
static bool   anim_TouchStarted = false;
static int    fastShotState     = 0;       // 0=rotate 1=hold 2=power 3=wait
static double stateStartTime    = 0.0;
static double startAngle        = 0.0;
static double g_lastFastShotTime = 0.0;
static double g_shotCooldownEnd  = 0.0;

// postShot lock — cegah re-scan sebelum shot selesai diproses engine
static bool   g_postShotLock   = false;
static double g_postShotAngle  = 0.0;
static double g_postShotPower  = 0.0;
static int    g_postShotFrames = 0;

static double nowSec() {
    return std::chrono::duration<double>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

namespace AutoPlay {
    double lastSetAngle = 0.0;
    bool   didSetAngle  = false;
    bool   bAutoPlaying = false;

    enum State {
        IDLE,
        SCANNING,
        NOMINATING,
        EXECUTING,
    } state = IDLE;

    double pendingShotPower = 0.0;
    double pendingShotAngle = 0.0;
    int    nominationFrameCounter = 0;

    enum ScanMode { FAST, SLOW } scan = FAST;

    bool shouldAutoPlay() {
        return !didSetAngle || lastSetAngle == sharedGameManager.mVisualCue().mVisualGuide().mAimAngle();
    }

    void setAimAngle(double angle) {
        lastSetAngle = angle;
        sharedGameManager.mVisualCue().mVisualGuide().mAimAngle(angle);
    }

    void setPower(double power) {
        sharedGameManager.mVisualCue().mPower(ShotPowerToPower(power));
    }

    void triggerShot() {
        g_postShotLock   = true;
        g_postShotAngle  = anim_TargetAngle;
        g_postShotPower  = anim_TargetPower;
        g_postShotFrames = 0;
        M(void, libmain + 0x2dc0c58, void*)(F(void*, sharedGameManager + 0x3b0));
        g_shotCooldownEnd = nowSec() + 0.5;
    }

    void ClearState() {
        g_CurrentCandidate.idx = -1;
        lastFailedCuePos = { -1000.0, -1000.0 };
        fastShotState    = 0;
        anim_IsPulling   = false;
        anim_RotationDone = false;

        if (anim_TouchStarted) {
            NativeTouchesEnd(5, Width * 0.83f, Height * 0.82f);
            anim_TouchStarted = false;
        }
        if (powerSlider.Active) {
            float sliderX = Width * 0.858f;
            float sliderY = Height * 0.18f;
            NativeTouchesEnd(powerSlider.TouchIndex, sliderX, sliderY);
            powerSlider.Active = false;
            powerSlider.state  = PowerSlider::IDLE;
        }
        if (buttonClicker.Active) {
            NativeTouchesEnd(buttonClicker.TouchIndex, buttonClicker.ClickPos.x, buttonClicker.ClickPos.y);
            buttonClicker.Active = false;
            buttonClicker.state  = ButtonClicker::IDLE;
        }
    }

    // takeShot — mulai animasi drag (tidak langsung tembak)
    void takeShot(double angle, double power) {
        anim_TargetAngle  = angle;
        anim_TargetPower  = power;
        anim_IsPulling    = true;
        anim_RotationDone = false;
        anim_TouchStarted = false;
        fastShotState     = 0;

        if (sharedGameManager && sharedGameManager.mVisualCue() &&
            sharedGameManager.mVisualCue().mVisualGuide()) {
            startAngle = sharedGameManager.mVisualCue().mVisualGuide().mAimAngle();
        } else {
            startAngle = angle;
        }
        stateStartTime = nowSec();
    }

    void Shoot(double angle, double power = 0.0) {
        setAimAngle(angle);
        gPrediction->determineShotResult(false, angle, power);

        bool nominating = false;
        int nominationMode = sharedGameManager.getPocketNominationMode();
        auto myclass = sharedGameManager.getPlayerClassification();
        if ((nominationMode == 1 && myclass == Ball::Classification::EIGHT_BALL) ||
            (nominationMode == 2 && myclass != Ball::Classification::ANY)) {
            if (g_CurrentCandidate.idx != -1 &&
                sharedGameManager.getNominatedPocket() != g_CurrentCandidate.pocketIndex) {
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
            state = EXECUTING;
        }
    }

    // ── Drag animation update — dipanggil tiap frame ─────────────────────────
    void UpdateDragAnim() {
        if (!anim_IsPulling) return;

        const float jX = Width  * 0.83f;
        const float jY = Height * 0.82f;
        const float jR = 65.0f;

        double elapsed = nowSec() - stateStartTime;

        // ── State 0: Joystick rotation sweep ─────────────────────────────────
        if (fastShotState == 0) {
            const double t1 = 0.20, t2 = 0.75, t3 = 1.00, t4 = 1.40, t5 = 1.60;

            double normalizedStart  = normalizeAngle(startAngle);
            double normalizedTarget = normalizeAngle(anim_TargetAngle);
            double delta = normalizedTarget - normalizedStart;
            if (delta >  M_PI) delta -= 2.0 * M_PI;
            if (delta < -M_PI) delta += 2.0 * M_PI;
            double dir = (delta >= 0) ? 1.0 : -1.0;

            double oppositeAngle  = normalizedStart - dir * (30.0 * M_PI / 180.0);
            double overshootAngle = normalizedTarget + dir * (20.0 * M_PI / 180.0);
            double nudgeAngle     = normalizedTarget - dir * (1.5  * M_PI / 180.0);
            double curAngle       = normalizedTarget;

            if (elapsed < t1) {
                double t = elapsed / t1;
                t = 1.0 - pow(1.0 - t, 3.0);
                curAngle = normalizedStart + (oppositeAngle - normalizedStart) * t;
                if (!anim_TouchStarted) {
                    anim_TouchStarted = true;
                    NativeTouchesBegin(5, jX, jY);
                }
            } else if (elapsed < t2) {
                double t = (elapsed - t1) / (t2 - t1);
                t = t * t * (3.0 - 2.0 * t);
                curAngle = oppositeAngle + (overshootAngle - oppositeAngle) * t;
            } else if (elapsed < t3) {
                double t = (elapsed - t2) / (t3 - t2);
                t = t * t * (3.0 - 2.0 * t);
                curAngle = overshootAngle + (nudgeAngle - overshootAngle) * t;
            } else if (elapsed < t4) {
                double t = (elapsed - t3) / (t4 - t3);
                t = sin(t * M_PI / 2.0);
                curAngle = nudgeAngle + (normalizedTarget - nudgeAngle) * t;
            } else if (elapsed < t5) {
                curAngle = normalizedTarget;
                if (!anim_RotationDone && elapsed > t5 - 0.05) {
                    anim_RotationDone = true;
                    setAimAngle(anim_TargetAngle);
                }
            }

            if (elapsed < t5) {
                setAimAngle(curAngle);
                NativeTouchesMove(5, jX + (float)cos(curAngle) * jR,
                                     jY + (float)sin(curAngle) * jR);
                return;
            }

            // Selesai rotate — snap ke target, lanjut state 1
            setAimAngle(anim_TargetAngle);
            NativeTouchesMove(5, jX + (float)cos(anim_TargetAngle) * jR,
                                 jY + (float)sin(anim_TargetAngle) * jR);
            stateStartTime = nowSec();
            fastShotState  = 1;
            return;
        }

        // Selalu lock angle di memory selama pull berlangsung
        setAimAngle(anim_TargetAngle);
        double elapsed_shot = nowSec() - stateStartTime;

        // ── State 1: Hold joystick sebentar lalu start power slider ──────────
        if (fastShotState == 1) {
            NativeTouchesMove(5, jX + (float)cos(anim_TargetAngle) * jR,
                                 jY + (float)sin(anim_TargetAngle) * jR);
            setAimAngle(anim_TargetAngle);

            if (elapsed_shot >= 0.10) {
                // Release joystick
                NativeTouchesEnd(5, jX + (float)cos(anim_TargetAngle) * jR,
                                    jY + (float)sin(anim_TargetAngle) * jR);

                // Power di-set di memory dulu biar akurat
                setPower(anim_TargetPower);

                // Ambil posisi slider dari persistent, fallback ke default
                float sliderXPercent = persistent_float[O("fPowerBarXPercent")];
                float sliderX = (sliderXPercent > 0.01f) ? Width * sliderXPercent : Width * 0.858f;
                float sliderYStart = persistent_float[O("fPowerBarYStartPercent")];
                float sliderYEnd   = persistent_float[O("fPowerBarYEndPercent")];
                sliderYStart = (sliderYStart > 0.01f) ? Height * sliderYStart : Height * 0.18f;
                sliderYEnd   = (sliderYEnd   > 0.01f) ? Height * sliderYEnd   : Height * 0.82f;

                ImVec4 rect(sliderX - 20.0f, sliderYStart, 40.0f, sliderYEnd - sliderYStart);
                powerSlider.SimulateDrag(rect, (float)anim_TargetPower, 0.85f, 0.40f);

                stateStartTime = nowSec();
                fastShotState  = 2;
            }
            return;
        }

        // ── State 2: Tunggu power slider selesai, lalu tembak ────────────────
        if (fastShotState == 2) {
            if (powerSlider.Active) {
                return; // tunggu slider selesai (termasuk RETURNING)
            }
            // Slider selesai — set angle + power di memory sekali lagi biar akurat
            setAimAngle(anim_TargetAngle);
            setPower(anim_TargetPower);
            triggerShot();
            stateStartTime = nowSec();
            fastShotState  = 3;
            return;
        }

        // ── State 3: Tunggu bola berhenti ────────────────────────────────────
        if (fastShotState == 3) {
            setAimAngle(anim_TargetAngle);

            static double s_stoppedAt = -1.0;
            if (s_stoppedAt < stateStartTime) s_stoppedAt = nowSec();

            bool timedOut = (nowSec() - stateStartTime > 12.0);

            // Cek bola masih bergerak
            bool ballsMoving = false;
            if (sharedGameManager) {
                Table table = sharedGameManager.mTable;
                if (table) {
                    auto& balls = table.mBalls();
                    for (int i = 0; i < 16 && balls; i++) {
                        auto b = balls[i];
                        if (b.instance) {
                            auto v = b.velocity();
                            if (v.x*v.x + v.y*v.y > 0.01) { ballsMoving = true; break; }
                        }
                    }
                }
            }

            if (ballsMoving && !timedOut) {
                s_stoppedAt = nowSec();
                return;
            }

            double settledFor = nowSec() - s_stoppedAt;
            if (settledFor < 0.5 && !timedOut) return;

            s_stoppedAt       = -1.0;
            anim_IsPulling    = false;
            anim_RotationDone = false;
            anim_TouchStarted = false;
            fastShotState     = 0;
            g_lastFastShotTime = nowSec();
            ClearState();
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
        auto& cueBall = gPrediction->guiData.balls[0];
        
        int steps = 0;
        bool foundShot = false;
        
        while (steps < 10 && currentScanAngle < maxAngle) {
            double angle = currentScanAngle;
            currentScanAngle += angleStep;
            steps++;

            std::vector<double> powers = {666.0, 466.0, 266.0, 100.0};
            for (double power : powers) {
                gPrediction->determineShotResult(true, angle, power, sharedGameManager.getShotSpin());
                
                bool isPotentiallyValid = false;
                int targetIdx = -1;

                bool bFoundLowestNumberedBall = false;
                int iFoundLowestNumberedBall = -1;
                bool isNineBallGame = myclass == Ball::Classification::NINE_BALL_RULE;

                if (isNineBallGame) {
                    for (int i = 1; i < gPrediction->guiData.ballsCount; i++) {
                        auto& ball = gPrediction->guiData.balls[i];
                        if (!ball.originalOnTable) continue;
                        bFoundLowestNumberedBall = true;
                        iFoundLowestNumberedBall = i;
                        break;
                    }

                    auto firstHit = gPrediction->guiData.collision.firstHitBall;
                    if (!firstHit) continue;
                    if (firstHit->index != iFoundLowestNumberedBall) continue;
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
                    targetIdx = bestPottedIdx;

                    g_CurrentCandidate.idx = targetIdx;
                    g_CurrentCandidate.angle = angle;
                    g_CurrentCandidate.power = power;
                    g_CurrentCandidate.pocketIndex = gPrediction->guiData.balls[targetIdx].pocketIndex;
                    foundShot = true;
                    Shoot(angle, power);
                    break;
                }

                for (int i = 1; i < gPrediction->guiData.ballsCount; i++) {
                    auto& ball = gPrediction->guiData.balls[i];
                    if (ball.originalOnTable && !ball.onTable) {
                        bool isValidTarget = false;
                        if (myclass == Ball::Classification::ANY) {
                            if (ball.classification != Ball::Classification::CUE_BALL &&
                                ball.classification != Ball::Classification::EIGHT_BALL)
                                isValidTarget = true;
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
            state = IDLE;
        }
    }
    
    void ScanFast(double angleStep = 0.1f) {
        if (g_CurrentCandidate.idx != -1) return;
        if (gPrediction->guiData.balls[0].initialPosition == lastFailedCuePos) return;

        double startingAngle = sharedGameManager.mVisualCue().mVisualGuide().mAimAngle();
        gPrediction->determineShotResult(true, startingAngle);

        Ball::Classification myclass = sharedGameManager.getPlayerClassification();
        uint nominatedPocket = sharedGameManager.getNominatedPocket();
        
        std::vector<Candidate> candidates;
        auto pockets = getPockets();
        auto& cueBall = gPrediction->guiData.balls[0];
        
        bool bFoundLowestNumberedBall = false;
        int iFoundLowestNumberedBall = -1;
        bool isNineBallGame = myclass == Ball::Classification::NINE_BALL_RULE;

        for (int i = 1; i < gPrediction->guiData.ballsCount; i++) {
            if (isNineBallGame && bFoundLowestNumberedBall) break;
            auto& ball = gPrediction->guiData.balls[i];
            if (!ball.originalOnTable) continue;

            if (!bFoundLowestNumberedBall) {
                bFoundLowestNumberedBall = true;
                iFoundLowestNumberedBall = i;
            }

            if (!isNineBallGame) {
                bool isACandidate = myclass == Ball::Classification::ANY
                    ? ball.classification != Ball::Classification::EIGHT_BALL
                    : ball.classification == myclass;
                if (!isACandidate) continue;
            }

            for (int pocketIdx = 0; pocketIdx < (int)pockets.size(); pocketIdx++) {
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

                double score = distCueToTarget + distTargetToPocket;
                constexpr double slidingDeceleration = 196.0;
                double power = sqrt(2.0 * slidingDeceleration * score);
                if (power > 666.0) power = 666.0;

                candidates.push_back({i, angle, score, pocketIdx, power});
            }
        }

        std::sort(candidates.begin(), candidates.end());

        bool foundShot = false;
        for (const auto& cand : candidates) {
            double angle = NumberUtils::normalizeDoublePrecision(normalizeAngle(cand.angle));
            gPrediction->determineShotResult(true, angle, cand.power, sharedGameManager.getShotSpin(), cand);
            if (!gPrediction->firstHitIsTarget) continue;
            if (!gPrediction->guiData.balls[0].onTable) continue;

            if (myclass == Ball::Classification::NINE_BALL_RULE) {
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
                if (nominatedPocket < 6 && gPrediction->guiData.balls[bestPottedIdx].pocketIndex != nominatedPocket) continue;

                g_CurrentCandidate = cand;
                g_CurrentCandidate.idx = bestPottedIdx;
                g_CurrentCandidate.pocketIndex = gPrediction->guiData.balls[bestPottedIdx].pocketIndex;
                foundShot = true;
                Shoot(angle, cand.power);
                break;
            }

            if (gPrediction->guiData.balls[cand.idx].onTable) continue;
            if (gPrediction->guiData.balls[cand.idx].pocketIndex != cand.pocketIndex) continue;

            bool isAngleGood = false;
            for (int i = 1; i < gPrediction->guiData.ballsCount; i++) {
                Prediction::Ball& ball = gPrediction->guiData.balls[i];
                bool match = (myclass == Ball::Classification::ANY)
                    ? (ball.classification != Ball::Classification::CUE_BALL &&
                       ball.classification != Ball::Classification::EIGHT_BALL)
                    : (ball.classification == myclass);
                if (match && ball.originalOnTable && !ball.onTable) isAngleGood = true;
            }

            if (isAngleGood && gPrediction->guiData.collision.firstHitBall) {
                auto firstHit = gPrediction->guiData.collision.firstHitBall;
                if (myclass != Ball::Classification::ANY && firstHit->classification != myclass) isAngleGood = false;
                else if (myclass == Ball::Classification::ANY && firstHit->classification == Ball::Classification::EIGHT_BALL) isAngleGood = false;
            }
            if (isAngleGood && !gPrediction->guiData.balls[0].onTable) isAngleGood = false;
            auto& eb = gPrediction->guiData.balls[8];
            if (isAngleGood && eb.originalOnTable && !eb.onTable && myclass != Ball::Classification::EIGHT_BALL) isAngleGood = false;

            if (isAngleGood) {
                g_CurrentCandidate = cand;
                foundShot = true;
                Shoot(angle, cand.power);
                break;
            }
        }

        if (!foundShot) {
            lastFailedCuePos = cueBall.initialPosition;
            scan = SLOW;
        }
    }

    void DrawToggleButton() {
        ImGuiIO& io = GetIO();
        float button_size = ImGui::GetFrameHeight() * 2.3f;
        float windowWidth  = button_size + GetStyle().WindowPadding.x * 2;
        float windowHeight = button_size + GetStyle().WindowPadding.y * 2;

        SetNextWindowPos(ImVec2(io.DisplaySize.x - 155 - windowWidth, io.DisplaySize.y - 20 - windowHeight), ImGuiCond_Always);
        SetNextWindowSize(ImVec2(windowWidth, windowHeight), ImGuiCond_Always);

        PushStyleColor(ImGuiCol_WindowBg, IM_COL32(0, 0, 0, 0));
        PushStyleColor(ImGuiCol_Border,   IM_COL32(0, 0, 0, 0));
        PushStyleVar(ImGuiStyleVar_WindowRounding, 5.0f);

        if (Begin("AutoPlay", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                  ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings)) {

            auto DrawPlayPauseButton = [&](bool isPause) -> bool {
                ImVec2 pos    = GetCursorScreenPos();
                ImVec2 size(button_size, button_size);
                ImVec2 end(pos.x + size.x, pos.y + size.y);
                ImVec2 center(pos.x + size.x * 0.5f, pos.y + size.y * 0.5f);

                PushStyleColor(ImGuiCol_Button,        IM_COL32(50, 50, 50, 180));
                PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(80, 80, 80, 200));
                PushStyleColor(ImGuiCol_ButtonActive,  IM_COL32(100,100,100, 200));
                PushStyleColor(ImGuiCol_Text,          IM_COL32(255,255,255, 255));

                bool clicked = Button("##AutoPlayBtn", size);
                ImDrawList* dl = GetWindowDrawList();
                float h = size.y * 0.4f, w = h * 0.8f;
                if (isPause) {
                    float bar_w = w * 0.35f, gap = w * 0.3f;
                    dl->AddRectFilled(ImVec2(center.x-gap/2-bar_w,center.y-h/2), ImVec2(center.x-gap/2,center.y+h/2), IM_COL32(255,255,255,180));
                    dl->AddRectFilled(ImVec2(center.x+gap/2,center.y-h/2), ImVec2(center.x+gap/2+bar_w,center.y+h/2), IM_COL32(255,255,255,180));
                } else {
                    float off_x = h * 0.3f;
                    dl->AddTriangleFilled(ImVec2(center.x-off_x,center.y-h/2), ImVec2(center.x-off_x,center.y+h/2), ImVec2(center.x+off_x*1.5f,center.y), IM_COL32(255,255,255,180));
                }
                GetForegroundDrawList()->AddRect(pos, end, IM_COL32(200,200,200,255), 5.0f, 0, 2.0f);
                PopStyleColor(4);
                return clicked;
            };

            if (DrawPlayPauseButton(bAutoPlaying)) {
                bAutoPlaying = !bAutoPlaying;
                if (bAutoPlaying) ClearState();
            }
        } End();
        PopStyleVar();
        PopStyleColor(2);
    }

    bool isAnimationActive() {
        auto visualCue = sharedGameManager.mVisualCue();
        if (!visualCue) return true;
        auto _powerBarView = F(ptr, visualCue + 0x510);
        if (!_powerBarView) return true;
        auto activeAction = M(ptr, libmain + 0x2de6f30, ptr)(_powerBarView);
        return activeAction != 0;
    }

    void Update() {
        buttonClicker.Update();
        powerSlider.Update();
        UpdateDragAnim();
        DrawToggleButton();

        // postShotLock — tunggu engine selesai proses shot
        if (g_postShotLock) {
            if (g_postShotFrames > 0) {
                setAimAngle(g_postShotAngle);
                setPower(g_postShotPower);
                g_postShotFrames--;
            } else {
                g_postShotLock = false;
                state = IDLE;
            }
            return;
        }

        // Kalau drag masih jalan, jangan scan
        if (anim_IsPulling) return;

        if (isAnimationActive()) return;

        if (!bAutoPlaying || !sharedGameManager.mStateManager().isPlayerTurn()) {
            state = IDLE;
            return;
        }

        // Cooldown setelah shot
        if (nowSec() < g_shotCooldownEnd) return;

        if (state == IDLE) {
            state = SCANNING;
            scan  = FAST;
        }
        if (state == SCANNING) {
            if (scan == FAST) ScanFast();
            if (scan == SLOW) {
                DrawEightBallLoading(GetForegroundDrawList());
                ScanSlow(0.003f);
            }
        }
        if (state == NOMINATING) {
            nominationFrameCounter++;
            if (nominationFrameCounter == 10) {
                buttonClicker.Click(GetPocketScreenPos(g_CurrentCandidate.pocketIndex));
            }
            if (nominationFrameCounter > 20 && !buttonClicker.Active) {
                takeShot(pendingShotAngle, pendingShotPower);
                state = EXECUTING;
            }
        }
        // EXECUTING — dihandle oleh UpdateDragAnim()
    }
};
