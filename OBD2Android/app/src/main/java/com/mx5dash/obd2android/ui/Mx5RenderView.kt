package com.mx5dash.obd2android.ui

import android.content.Context
import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Matrix
import android.graphics.Paint
import android.graphics.Path
import android.graphics.RadialGradient
import android.graphics.RectF
import android.graphics.Shader
import android.util.AttributeSet
import android.view.MotionEvent
import android.view.SurfaceHolder
import android.view.SurfaceView
import com.mx5dash.obd2android.Mx5Application
import com.mx5dash.obd2android.bridge.NativeBridge
import kotlin.math.sin

class Mx5RenderView @JvmOverloads constructor(
    context: Context,
    attrs: AttributeSet? = null,
    defStyle: Int = 0
) : SurfaceView(context, attrs, defStyle), SurfaceHolder.Callback {

    companion object {
        const val LOGICAL_W = 800
        const val LOGICAL_H = 360
        private const val CONTENT_SCREEN_COUNT = 7 // Screens 0..6
        private const val SLIDE_DURATION_MS = 200L
        private const val MAX_HOLD_TIMEOUT_MS = 750L
    }

    private enum class TransitionPhase {
        IDLE,
        SLIDING_OUT_SCREEN_A,
        HOLD_LOCK,
        SLIDING_IN_SCREEN_B
    }

    private enum class Direction {
        LEFT,  // User swiped left -> going to next screen right -> car faces left
        RIGHT  // User swiped right -> going to prev screen left -> car faces right
    }

    private var renderThread: Thread? = null
    @Volatile private var isRunning = false

    private val argbBuffer = IntArray(LOGICAL_W * LOGICAL_H)
    private val mainBitmap = Bitmap.createBitmap(LOGICAL_W, LOGICAL_H, Bitmap.Config.ARGB_8888)
    private val screenABitmap = Bitmap.createBitmap(LOGICAL_W, LOGICAL_H, Bitmap.Config.ARGB_8888)
    private val screenBBitmap = Bitmap.createBitmap(LOGICAL_W, LOGICAL_H, Bitmap.Config.ARGB_8888)

    private val paint = Paint(Paint.FILTER_BITMAP_FLAG)
    private val scaleRect = RectF()
    private val tempRect = RectF()
    private val forwardMatrix = Matrix()
    private val inverseMatrix = Matrix()
    private val touchPts = FloatArray(2)

    // Transition State Machine
    @Volatile private var transitionPhase = TransitionPhase.IDLE
    private var transitionDirection = Direction.LEFT
    private var targetScreenIndex = 0
    private var phaseStartTimeMs = 0L
    private var holdStartTimeMs = 0L

    // Touch Handling State
    private var downLogicalX = 0f
    private var downLogicalY = 0f
    private var downTimeMs = 0L
    private var isSwipeHandled = false

    // Vehicle Silhouette Paints
    private val carBodyPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.rgb(18, 20, 26)
        style = Paint.Style.FILL
    }
    private val carOutlinePaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.rgb(220, 224, 232) // Satin Chrome #DCE0E8
        style = Paint.Style.STROKE
        strokeWidth = 2.0f
    }
    private val carSillPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.rgb(209, 34, 41) // Soul Red #D12229
        style = Paint.Style.STROKE
        strokeWidth = 2.0f
    }
    private val carWindowPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.rgb(8, 9, 12)
        style = Paint.Style.FILL
    }
    private val carWindowTrimPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.rgb(182, 188, 200) // Silver #B6BCC8
        style = Paint.Style.STROKE
        strokeWidth = 1.2f
    }
    private val wheelTirePaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.rgb(28, 30, 36)
        style = Paint.Style.FILL
    }
    private val wheelRimPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.rgb(220, 224, 232)
        style = Paint.Style.STROKE
        strokeWidth = 1.8f
    }
    private val wheelHubPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.rgb(209, 34, 41)
        style = Paint.Style.FILL
    }
    private val drlPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.rgb(245, 158, 11) // Amber #F59E0B
        style = Paint.Style.FILL
    }
    private val drlGlowPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.rgb(245, 158, 11)
        style = Paint.Style.FILL
    }
    private val tailPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.rgb(225, 29, 72) // Soul Red Glow
        style = Paint.Style.FILL
    }
    private val tailGlowPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.rgb(225, 29, 72)
        style = Paint.Style.FILL
    }
    private val textPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.rgb(220, 224, 232)
        textSize = 14f
        textAlign = Paint.Align.CENTER
        isFakeBoldText = true
        letterSpacing = 0.08f
    }
    private val dotPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.rgb(220, 30, 45) // Mazda Red Dot
        style = Paint.Style.FILL
    }

    private var carLeftBitmap: Bitmap? = null
    private var carRightBitmap: Bitmap? = null

    private val app: Mx5Application?
        get() = context.applicationContext as? Mx5Application

    init {
        holder.addCallback(this)
        try {
            context.assets.open("mx5_rf_side_left.png").use { stream ->
                carLeftBitmap = android.graphics.BitmapFactory.decodeStream(stream)
            }
            context.assets.open("mx5_rf_side_right.png").use { stream ->
                carRightBitmap = android.graphics.BitmapFactory.decodeStream(stream)
            }
        } catch (e: Exception) {
            android.util.Log.w("Mx5RenderView", "Could not load side car assets: ${e.message}")
        }
    }

    override fun surfaceCreated(holder: SurfaceHolder) {
        isRunning = true
        renderThread = Thread({
            while (isRunning && holder.surface.isValid) {
                val nowMs = System.currentTimeMillis()

                // Render current LVGL native frame
                val updated = NativeBridge.nativeRender(argbBuffer, argbBuffer.size)
                if (updated) {
                    mainBitmap.setPixels(argbBuffer, 0, LOGICAL_W, 0, 0, LOGICAL_W, LOGICAL_H)
                }

                // Sync background OBD polling screen if idle
                if (transitionPhase == TransitionPhase.IDLE) {
                    val currentNative = NativeBridge.nativeGetCurrentScreen()
                    app?.bluetoothManager?.let { bm ->
                        if (bm.currentActiveScreen != currentNative) {
                            bm.currentActiveScreen = currentNative
                        }
                    }
                }

                val canvas = holder.lockCanvas()
                if (canvas != null) {
                    try {
                        canvas.drawColor(Color.rgb(12, 13, 15)) // Mazda C_BG obsidian letterbox
                        computeScaleRect(width.toFloat(), height.toFloat())

                        when (transitionPhase) {
                            TransitionPhase.IDLE -> {
                                canvas.drawBitmap(mainBitmap, null, scaleRect, paint)
                            }

                            TransitionPhase.SLIDING_OUT_SCREEN_A -> {
                                val progress = ((nowMs - phaseStartTimeMs).toFloat() / SLIDE_DURATION_MS).coerceIn(0f, 1f)
                                val ease = progress * progress * (3f - 2f * progress)

                                // Slide Screen A off-screen
                                val shiftA = if (transitionDirection == Direction.LEFT) {
                                    -scaleRect.width() * ease
                                } else {
                                    scaleRect.width() * ease
                                }
                                tempRect.set(scaleRect.left + shiftA, scaleRect.top, scaleRect.right + shiftA, scaleRect.bottom)
                                canvas.drawBitmap(screenABitmap, null, tempRect, paint)

                                // Slide Vehicle Silhouette towards center (400, 180)
                                val carX = if (transitionDirection == Direction.LEFT) {
                                    (LOGICAL_W + 140f) - ((LOGICAL_W + 140f - 400f) * ease)
                                } else {
                                    -140f + ((400f - (-140f)) * ease)
                                }
                                val pulse = (sin(nowMs * 0.008) * 0.5 + 0.5).toFloat()
                                drawMx5Silhouette(canvas, carX, 180f, transitionDirection == Direction.LEFT, pulse, getScreenTitle(targetScreenIndex))

                                if (progress >= 1.0f) {
                                    transitionPhase = TransitionPhase.HOLD_LOCK
                                    holdStartTimeMs = nowMs
                                }
                            }

                            TransitionPhase.HOLD_LOCK -> {
                                // Draw centered silhouette with pulsing amber DRLs
                                val pulse = (sin((nowMs - holdStartTimeMs) * 0.008) * 0.5 + 0.5).toFloat()
                                drawMx5Silhouette(canvas, 400f, 180f, transitionDirection == Direction.LEFT, pulse, getScreenTitle(targetScreenIndex))

                                val isPayloadReady = app?.bluetoothManager?.isFreshPayloadReady == true
                                val isTimeout = (nowMs - holdStartTimeMs) >= MAX_HOLD_TIMEOUT_MS

                                if (isPayloadReady || isTimeout) {
                                    // Switch native engine screen
                                    NativeBridge.nativeSetScreen(targetScreenIndex)
                                    // Capture Screen B
                                    NativeBridge.nativeRender(argbBuffer, argbBuffer.size)
                                    screenBBitmap.setPixels(argbBuffer, 0, LOGICAL_W, 0, 0, LOGICAL_W, LOGICAL_H)

                                    transitionPhase = TransitionPhase.SLIDING_IN_SCREEN_B
                                    phaseStartTimeMs = nowMs
                                }
                            }

                            TransitionPhase.SLIDING_IN_SCREEN_B -> {
                                val progress = ((nowMs - phaseStartTimeMs).toFloat() / SLIDE_DURATION_MS).coerceIn(0f, 1f)
                                val ease = progress * progress * (3f - 2f * progress)

                                // Slide Screen B in from incoming side towards center
                                val shiftB = if (transitionDirection == Direction.LEFT) {
                                    scaleRect.width() * (1f - ease)
                                } else {
                                    -scaleRect.width() * (1f - ease)
                                }
                                tempRect.set(scaleRect.left + shiftB, scaleRect.top, scaleRect.right + shiftB, scaleRect.bottom)
                                canvas.drawBitmap(screenBBitmap, null, tempRect, paint)

                                // Accelerate Vehicle Silhouette off-screen in its facing direction
                                val carX = if (transitionDirection == Direction.LEFT) {
                                    400f - (540f * ease)
                                } else {
                                    400f + (540f * ease)
                                }
                                val pulse = (sin(nowMs * 0.008) * 0.5 + 0.5).toFloat()
                                drawMx5Silhouette(canvas, carX, 180f, transitionDirection == Direction.LEFT, pulse, "")

                                if (progress >= 1.0f) {
                                    transitionPhase = TransitionPhase.IDLE
                                    app?.bluetoothManager?.targetTransitionScreen = -1
                                    app?.bluetoothManager?.currentActiveScreen = targetScreenIndex
                                }
                            }
                        }

                    } finally {
                        holder.unlockCanvasAndPost(canvas)
                    }
                }

                try {
                    Thread.sleep(16) // ~60 FPS smooth rendering
                } catch (_: InterruptedException) {
                    break
                }
            }
        }, "Mx5RenderThread").apply {
            isDaemon = true
            start()
        }
    }

    override fun surfaceChanged(holder: SurfaceHolder, format: Int, w: Int, h: Int) {
        computeScaleRect(w.toFloat(), h.toFloat())
    }

    override fun surfaceDestroyed(holder: SurfaceHolder) {
        isRunning = false
        renderThread?.interrupt()
        renderThread = null
    }

    private fun computeScaleRect(viewW: Float, viewH: Float) {
        val scale = minOf(viewW / LOGICAL_W, viewH / LOGICAL_H)
        val dstW = LOGICAL_W * scale
        val dstH = LOGICAL_H * scale
        val dstX = (viewW - dstW) / 2f
        val dstY = (viewH - dstH) / 2f

        scaleRect.set(dstX, dstY, dstX + dstW, dstY + dstH)

        forwardMatrix.reset()
        forwardMatrix.postScale(scale, scale)
        forwardMatrix.postTranslate(dstX, dstY)
        forwardMatrix.invert(inverseMatrix)
    }

    private fun drawMx5Silhouette(
        canvas: Canvas,
        logicalCx: Float,
        logicalCy: Float,
        isFacingLeft: Boolean,
        pulse: Float,
        screenTitle: String
    ) {
        val scale = scaleRect.width() / LOGICAL_W
        val cx = scaleRect.left + (logicalCx * scale)
        val cy = scaleRect.top + (logicalCy * scale)

        canvas.save()
        canvas.translate(cx, cy)
        canvas.scale(scale, scale)

        val carBitmap = if (isFacingLeft) carLeftBitmap else carRightBitmap
        if (carBitmap != null) {
            val carW = 380f
            val carH = (carW * carBitmap.height / carBitmap.width)
            val dstLeft = -carW / 2f
            val dstTop = -carH / 2f
            tempRect.set(dstLeft, dstTop, dstLeft + carW, dstTop + carH)
            canvas.drawBitmap(carBitmap, null, tempRect, paint)

            // Pulsing Headlight & Taillight Glow overlays
            val drlAlpha = (140 + (115 * pulse)).toInt().coerceIn(0, 255)
            drlPaint.alpha = drlAlpha
            drlGlowPaint.alpha = (90 * pulse).toInt().coerceIn(0, 255)
            // Left headlight fixture
            canvas.drawCircle(-160f, 0f, 9f, drlGlowPaint)
            canvas.drawCircle(-160f, 0f, 3.5f, drlPaint)

            // Right taillight fixture
            val tailAlpha = (160 + (95 * pulse)).toInt().coerceIn(0, 255)
            tailPaint.alpha = tailAlpha
            tailGlowPaint.alpha = (95 * pulse).toInt().coerceIn(0, 255)
            canvas.drawCircle(160f, -6f, 8f, tailGlowPaint)
            canvas.drawCircle(160f, -6f, 3f, tailPaint)
        } else {
            // Fallback Vector Rendering if bitmap not loaded
            val bodyPath = Path().apply {
                moveTo(-110f, 20f)
                lineTo(-106f, 10f)
                quadTo(-102f, 4f, -80f, 3f)
                quadTo(-48f, 1f, -34f, -3f)
                lineTo(-12f, -26f)
                quadTo(10f, -28f, 26f, -26f)
                quadTo(54f, -8f, 70f, 4f)
                lineTo(96f, 6f)
                lineTo(106f, 10f)
                lineTo(102f, 20f)
                lineTo(82f, 22f)
                arcTo(46f, 6f, 82f, 42f, 0f, -180f, false)
                lineTo(-46f, 22f)
                arcTo(-82f, 6f, -46f, 42f, 0f, -180f, false)
                lineTo(-110f, 20f)
                close()
            }
            canvas.drawPath(bodyPath, carBodyPaint)
            canvas.drawPath(bodyPath, carOutlinePaint)

            val sillPath = Path().apply {
                moveTo(-44f, 21f)
                lineTo(44f, 21f)
            }
            canvas.drawPath(sillPath, carSillPaint)

            val windowPath = Path().apply {
                moveTo(-30f, -2f)
                lineTo(-10f, -23f)
                quadTo(8f, -24f, 22f, -22f)
                quadTo(42f, -8f, 52f, 0f)
                lineTo(-30f, 0f)
                close()
            }
            canvas.drawPath(windowPath, carWindowPaint)
            canvas.drawPath(windowPath, carWindowTrimPaint)

            drawWheel(canvas, -64f, 22f)
            drawWheel(canvas, 64f, 22f)

            val drlAlpha = (160 + (95 * pulse)).toInt().coerceIn(0, 255)
            drlPaint.alpha = drlAlpha
            drlGlowPaint.alpha = (85 * pulse).toInt().coerceIn(0, 255)
            canvas.drawCircle(-100f, 6f, 9f, drlGlowPaint)
            canvas.drawCircle(-100f, 6f, 3.5f, drlPaint)

            val tailAlpha = (180 + (75 * pulse)).toInt().coerceIn(0, 255)
            tailPaint.alpha = tailAlpha
            tailGlowPaint.alpha = (90 * pulse).toInt().coerceIn(0, 255)
            canvas.drawCircle(100f, 7f, 8f, tailGlowPaint)
            canvas.drawCircle(100f, 7f, 3f, tailPaint)
        }

        canvas.restore()

        // Screen Target Caption & Status
        if (screenTitle.isNotEmpty()) {
            val titleAlpha = (170 + (85 * pulse)).toInt().coerceIn(0, 255)
            textPaint.alpha = titleAlpha
            canvas.drawText(screenTitle, cx, cy + (76f * scale), textPaint)

            dotPaint.alpha = titleAlpha
            val titleHalfW = (textPaint.measureText(screenTitle) / 2f)
            canvas.drawCircle(cx - titleHalfW - (14f * scale), cy + (72f * scale), 3.5f * scale, dotPaint)
            canvas.drawCircle(cx + titleHalfW + (14f * scale), cy + (72f * scale), 3.5f * scale, dotPaint)
        }
    }

    private fun drawWheel(canvas: Canvas, wx: Float, wy: Float) {
        canvas.drawCircle(wx, wy, 15f, wheelTirePaint)
        canvas.drawCircle(wx, wy, 11f, wheelRimPaint)
        canvas.drawCircle(wx, wy, 4f, wheelHubPaint)
    }

    private fun getScreenTitle(screenIndex: Int): String {
        return when (screenIndex) {
            0 -> "HERO SPEEDOMETER"
            1 -> "TPMS & TIRE TEMPS"
            2 -> "TEMPERATURES & FLUIDS"
            3 -> "DIAGNOSTIC HUB"
            4 -> "TRACK & DYNAMICS"
            5 -> "ENGINE TACHOMETER"
            6 -> "FUEL & TRIP ECONOMY"
            7 -> "QUICK LAUNCHER MENU"
            8 -> "FUEL TRIMS & HPFP"
            9 -> "CYLINDERS & MISFIRE"
            10 -> "CHASSIS DYNAMICS & G-FORCE"
            11 -> "I/M SMOG READINESS"
            12 -> "DATA LOGS & BLACK BOX"
            13 -> "SYSTEM SETTINGS"
            14 -> "BLE CONNECTION"
            15 -> "SETUP WIZARD"
            16 -> "WHEEL CALIBRATION"
            else -> "TELEMETRY DASHBOARD"
        }
    }

    private fun startDirectionalTransition(targetScreen: Int, direction: Direction) {
        if (transitionPhase != TransitionPhase.IDLE) return

        // Capture current Screen A
        screenABitmap.setPixels(argbBuffer, 0, LOGICAL_W, 0, 0, LOGICAL_W, LOGICAL_H)

        targetScreenIndex = targetScreen
        transitionDirection = direction
        transitionPhase = TransitionPhase.SLIDING_OUT_SCREEN_A
        phaseStartTimeMs = System.currentTimeMillis()

        // Instruct Bluetooth background thread to prioritize target screen PIDs immediately
        app?.bluetoothManager?.let {
            it.targetTransitionScreen = targetScreen
            it.isFreshPayloadReady = false
        }
    }

    override fun onTouchEvent(event: MotionEvent): Boolean {
        touchPts[0] = event.x
        touchPts[1] = event.y
        inverseMatrix.mapPoints(touchPts)

        val logicalX = touchPts[0].coerceIn(0f, (LOGICAL_W - 1).toFloat())
        val logicalY = touchPts[1].coerceIn(0f, (LOGICAL_H - 1).toFloat())

        when (event.actionMasked) {
            MotionEvent.ACTION_DOWN -> {
                downLogicalX = logicalX
                downLogicalY = logicalY
                downTimeMs = System.currentTimeMillis()
                isSwipeHandled = false

                if (transitionPhase == TransitionPhase.IDLE) {
                    NativeBridge.nativeTouch(0, logicalX.toInt(), logicalY.toInt())
                }
            }

            MotionEvent.ACTION_MOVE -> {
                if (transitionPhase == TransitionPhase.IDLE && !isSwipeHandled) {
                    val dx = logicalX - downLogicalX
                    val dy = logicalY - downLogicalY

                    // Horizontal swipe trigger threshold (40px)
                    if (Math.abs(dx) > 40f && Math.abs(dx) > Math.abs(dy) * 1.25f) {
                        isSwipeHandled = true
                        NativeBridge.nativeTouch(1, logicalX.toInt(), logicalY.toInt())

                        val currentScreen = NativeBridge.nativeGetCurrentScreen()
                        if (currentScreen in 8..12) {
                            // Diagnostic Sub-Screens cycle among themselves (8..12)
                            if (dx < 0) {
                                val nextSub = 8 + ((currentScreen - 8 + 1) % 5)
                                startDirectionalTransition(nextSub, Direction.LEFT)
                            } else {
                                val prevSub = 8 + ((currentScreen - 8 + 4) % 5)
                                startDirectionalTransition(prevSub, Direction.RIGHT)
                            }
                        } else if (currentScreen < CONTENT_SCREEN_COUNT) {
                            if (dx < 0) {
                                // Swipe Left -> Next Screen (Car enters from right, moves left)
                                val next = (currentScreen + 1) % CONTENT_SCREEN_COUNT
                                startDirectionalTransition(next, Direction.LEFT)
                            } else {
                                // Swipe Right -> Prev Screen (Car enters from left, moves right)
                                val prev = (currentScreen + CONTENT_SCREEN_COUNT - 1) % CONTENT_SCREEN_COUNT
                                startDirectionalTransition(prev, Direction.RIGHT)
                            }
                        }
                    } else if (Math.abs(dy) > 55f && Math.abs(dy) > Math.abs(dx) * 1.25f) {
                        // Vertical swipe
                        // Restrict Swipe UP (dy < 0) to top 2/3 of screen so Android home/nav gestures are preserved
                        val isSwipeUp = dy < 0
                        val allowed = !isSwipeUp || (downLogicalY <= (LOGICAL_H * 2f / 3f))

                        if (allowed) {
                            isSwipeHandled = true
                            NativeBridge.nativeTouch(1, logicalX.toInt(), logicalY.toInt())
                            val currentScreen = NativeBridge.nativeGetCurrentScreen()
                            if (currentScreen in 8..12) {
                                // Exit sub-screen back to Diagnostic Hub (SCREEN_DIAG = 3)
                                startDirectionalTransition(3, if (dy < 0) Direction.LEFT else Direction.RIGHT)
                            } else {
                                NativeBridge.nativeToggleMenu()
                            }
                        }
                    } else {
                        NativeBridge.nativeTouch(0, logicalX.toInt(), logicalY.toInt())
                    }
                }
            }

            MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> {
                if (!isSwipeHandled && transitionPhase == TransitionPhase.IDLE) {
                    NativeBridge.nativeTouch(1, logicalX.toInt(), logicalY.toInt())
                }
            }
        }
        return true
    }
}
