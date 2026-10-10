package com.github.dayvil.platypus_watch.presentation

import android.os.Bundle
import android.view.WindowManager
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.unit.dp
import androidx.wear.compose.material3.AppScaffold
import com.github.dayvil.platypus_watch.presentation.theme.PlatypusWatchTheme
import com.github.dayvil.platypus_watch.presentation.view.MainScreenView

class MainActivity : ComponentActivity() {
    private lateinit var platypusMQTT: PlatypusMQTT

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        setContent {
            PlatypusWatchApp()
        }
    }
}

@Composable
fun PlatypusWatchApp() {
    PlatypusWatchTheme {
        AppScaffold {
            Box(
                modifier = Modifier
                    .fillMaxSize()
                    .padding(24.dp),
                contentAlignment = Alignment.Center,
            ) {
                MainScreenView()
            }
        }
    }
}

@Preview(device = "id:wearos_small_round", showSystemUi = true)
@Composable
private fun PlatypusWatchAppPreview() {
    PlatypusWatchApp()
}
