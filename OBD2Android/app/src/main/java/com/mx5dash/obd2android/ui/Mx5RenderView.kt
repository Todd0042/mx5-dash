package com.mx5dash.obd2android.ui

import android.content.Context
import android.graphics.Bitmap
import android.graphics.Color
import android.graphics.Matrix
import android.graphics.Paint
import android.graphics.RectF
import android.util.AttributeSet
import android.view.MotionEvent
import android.view.SurfaceHolder
import android.view.SurfaceView
import com.mx5dash.obd2android.bridge.NativeBridge

class Mx5RenderView @JvmOverloads constructor(
    context: Context,
    attrs: AttributeSet? = null,
    defStyle: Int = 0
) : SurfaceView(context, attrs, defStyle), SurfaceHolder.Callback {

    companion object {
        const val LOGICAL_W = 800
        const val LOGICAL_H = 360
    }

    private var renderThread: Thread? = null
    @Volatile private var isRunning = false

    private val argbBuffer = IntArray(LOGICAL_W * LOGICAL_H)
    private val paint = Paint(Paint.FILTER_BITMAP_FLAG)
    private val scaleRect = RectF()
    private val forwardMatrix = Matrix()
    private val inverseMatrix = Matrix()
    private val touchPts = FloatArray(2)

    init {
        holder.addCallback(this)
        isFocusable = true
    }

    override fun surfaceCreated(holder: SurfaceHolder) {
        isRunning = true
        renderThread = Thread({
            val frameBitmap = Bitmap.createBitmap(LOGICAL_W, LOGICAL_H, Bitmap.Config.ARGB_8888)
            while (isRunning && holder.surface.isValid) {
                val updated = NativeBridge.nativeRender(argbBuffer, argbBuffer.size)
                if (updated) {
                    frameBitmap.setPixels(argbBuffer, 0, LOGICAL_W, 0, 0, LOGICAL_W, LOGICAL_H)
                }

                val canvas = holder.lockCanvas()
                if (canvas != null) {
                    try {
                        canvas.drawColor(Color.rgb(12, 13, 15)) // Mazda C_BG letterbox
                        computeScaleRect(width.toFloat(), height.toFloat())
                        canvas.drawBitmap(frameBitmap, null, scaleRect, paint)
                    } finally {
                        holder.unlockCanvasAndPost(canvas)
                    }
                }

                try {
                    Thread.sleep(16) // ~60 FPS
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

    override fun onTouchEvent(event: MotionEvent): Boolean {
        touchPts[0] = event.x
        touchPts[1] = event.y
        inverseMatrix.mapPoints(touchPts)

        val logicalX = touchPts[0].toInt().coerceIn(0, LOGICAL_W - 1)
        val logicalY = touchPts[1].toInt().coerceIn(0, LOGICAL_H - 1)

        when (event.actionMasked) {
            MotionEvent.ACTION_DOWN -> {
                NativeBridge.nativeTouch(0, logicalX, logicalY)
            }
            MotionEvent.ACTION_MOVE -> {
                NativeBridge.nativeTouch(0, logicalX, logicalY)
            }
            MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> {
                NativeBridge.nativeTouch(1, logicalX, logicalY)
            }
        }
        return true
    }
}
