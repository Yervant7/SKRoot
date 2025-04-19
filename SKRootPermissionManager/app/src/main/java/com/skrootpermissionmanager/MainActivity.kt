package com.skrootpermissionmanager

import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context
import android.content.SharedPreferences
import android.content.pm.ApplicationInfo
import android.content.pm.PackageInfo
import android.graphics.drawable.Drawable
import android.os.Bundle
import android.widget.Toast
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.text.selection.SelectionContainer
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Clear
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.Checkbox
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.LocalTextStyle
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TextField
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.*
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.TextFieldValue
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties
import androidx.core.graphics.drawable.toBitmap
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import org.json.JSONArray
import java.net.URLDecoder
import java.nio.file.Paths
import androidx.core.content.edit
import com.skrootpermissionmanager.ui.theme.SKRootPermissionManagerTheme

// --- Data Models (Simplified from original Recycler Items) ---
data class AppInfo(
    val packageInfo: PackageInfo,
    val showName: String,
    val packageName: String,
    val icon: Drawable?
)

data class FileInfo(
    val filePath: String,
    val fileName: String,
    val description: String,
    val textColor: Color
)

class MainActivity : ComponentActivity() {

    // --- Native Method Declarations ---
    // (Keep these exactly as they are)
    private external fun testRoot(rootKey: String): String
    private external fun runRootCmd(rootKey: String, cmd: String): String
    private external fun runInit64ProcessCmd(rootKey: String, cmd: String): String
    private external fun installSu(rootKey: String, basePath: String): String
    private external fun getLastInstallSuFullPath(): String
    private external fun uninstallSu(rootKey: String, basePath: String): String
    private external fun autoSuEnvInject(rootKey: String, targetProcessCmdline: String): String
    private external fun getAllCmdlineProcess(rootKey: String): String
    private external fun parasitePrecheckApp(rootKey: String, targetProcessCmdline: String): String
    private external fun parasiteImplantApp(
        rootKey: String,
        targetProcessCmdline: String,
        targetSoFullPath: String
    ): String

    private external fun parasiteImplantSuEnv(
        rootKey: String,
        targetProcessCmdline: String,
        targetSoFullPath: String
    ): String

    companion object {
        // Used to load the 'permissionmanager' library on application startup.
        init {
            System.loadLibrary("permissionmanager")
        }
        const val DEFAULT_ROOT_KEY = "m3B3xOohsF3pu3gYndWo5iwsXcxAiapzXgq1DNeP5POd2z64"
        const val DEFAULT_SU_BASE_PATH = "/data/local/tmp"
        const val DEFAULT_LAST_CMD = "id"
        val RECOMMEND_FILES = arrayOf("libc++_shared.so")
    }

    // SharedPreferences handled via rememberSavable or LaunchedEffect
    private lateinit var prefs: SharedPreferences

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        prefs = getSharedPreferences("zhcs", Context.MODE_PRIVATE)

        setContent {
            SKRootPermissionManagerTheme { // Apply your app theme
                MainScreen(
                    prefs = prefs,
                    nativeFunctions = NativeFunctions(
                        testRoot = ::testRoot,
                        runRootCmd = ::runRootCmd,
                        runInit64ProcessCmd = ::runInit64ProcessCmd,
                        installSu = ::installSu,
                        getLastInstallSuFullPath = ::getLastInstallSuFullPath,
                        uninstallSu = ::uninstallSu,
                        autoSuEnvInject = ::autoSuEnvInject,
                        getAllCmdlineProcess = ::getAllCmdlineProcess,
                        parasitePrecheckApp = ::parasitePrecheckApp,
                        parasiteImplantApp = ::parasiteImplantApp,
                        parasiteImplantSuEnv = ::parasiteImplantSuEnv
                    )
                )
            }
        }
    }
}

// Data class to pass native functions easily
data class NativeFunctions(
    val testRoot: (String) -> String,
    val runRootCmd: (String, String) -> String,
    val runInit64ProcessCmd: (String, String) -> String,
    val installSu: (String, String) -> String,
    val getLastInstallSuFullPath: () -> String,
    val uninstallSu: (String, String) -> String,
    val autoSuEnvInject: (String, String) -> String,
    val getAllCmdlineProcess: (String) -> String,
    val parasitePrecheckApp: (String, String) -> String,
    val parasiteImplantApp: (String, String, String) -> String,
    val parasiteImplantSuEnv: (String, String, String) -> String
)

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun MainScreen(prefs: SharedPreferences, nativeFunctions: NativeFunctions) {
    val context = LocalContext.current
    val scope = rememberCoroutineScope() // Scope for launching coroutines

    // --- State Variables ---
    var rootKey by rememberSaveable {
        mutableStateOf(prefs.getString("rootKey", MainActivity.DEFAULT_ROOT_KEY) ?: MainActivity.DEFAULT_ROOT_KEY)
    }
    var lastInputCmd by rememberSaveable {
        mutableStateOf(prefs.getString("lastInputCmd", MainActivity.DEFAULT_LAST_CMD) ?: MainActivity.DEFAULT_LAST_CMD)
    }
    val suBasePath = MainActivity.DEFAULT_SU_BASE_PATH // Constant
    var consoleOutput by rememberSaveable(stateSaver = TextFieldValue.Saver) {
        mutableStateOf(TextFieldValue(""))
    }
    var isLoading by remember { mutableStateOf(false) }
    var loadingMessage by remember { mutableStateOf("") }

    var showRootKeyDialog by remember { mutableStateOf(false) }
    var showRootCmdDialog by remember { mutableStateOf(false) }
    var showInitCmdDialog by remember { mutableStateOf(false) }
    var showMsgDialog by remember { mutableStateOf<Pair<String, String>?>(null) } // Title, Message
    var showAppSelectionDialog by remember { mutableStateOf<AppSelectionConfig?>(null) } // Config for app selection
    var showFileSelectionDialog by remember { mutableStateOf<FileSelectionConfig?>(null) } // Config for file selection
    var showSuInjectModeDialog by remember { mutableStateOf(false) }

    // --- Effects ---
    LaunchedEffect(Unit) {
        showRootKeyDialog = true
    }

    // Effect to save state changes back to SharedPreferences
    LaunchedEffect(rootKey) {
        prefs.edit { putString("rootKey", rootKey) }
    }
    LaunchedEffect(lastInputCmd) {
        prefs.edit { putString("lastInputCmd", lastInputCmd) }
    }

    // --- Helper Functions ---
    fun appendConsoleMsg(msg: String) {
        val currentText = consoleOutput.text
        val newText = if (currentText.isEmpty()) msg else "$currentText\n\n$msg"
        consoleOutput = TextFieldValue(newText, selection = androidx.compose.ui.text.TextRange(newText.length))
    }

    fun cleanConsoleMsg() {
        consoleOutput = TextFieldValue("")
    }

    fun copyToClipboard(text: String) {
        val clipboard = context.getSystemService(Context.CLIPBOARD_SERVICE) as ClipboardManager
        val clip = ClipData.newPlainText("Copied Text", text)
        clipboard.setPrimaryClip(clip)
        Toast.makeText(context, "Copied successfully", Toast.LENGTH_SHORT).show()
    }

    // Coroutine launching helper
    fun runBackgroundTask(
        loadingMsg: String = "Processing...",
        task: suspend CoroutineScope.() -> String // Task returns result string
    ) {
        scope.launch {
            loadingMessage = loadingMsg
            isLoading = true
            val result = try {
                withContext(Dispatchers.IO) { // Run native calls off the main thread
                    task()
                }
            } catch (e: Exception) {
                "Error: ${e.message}"
            } finally {
                isLoading = false
            }
            appendConsoleMsg(result)
        }
    }

    fun runBackgroundTaskWithAppSelection(
        loadingMsg: String = "Processing...",
        appInfo: AppInfo,
        task: suspend CoroutineScope.(AppInfo) -> String // Task returns result string
    ) {
        scope.launch {
            loadingMessage = loadingMsg + " [${appInfo.showName}]"
            isLoading = true
            val result = try {
                withContext(Dispatchers.IO) { // Run native calls off the main thread
                    task(appInfo)
                }
            } catch (e: Exception) {
                "Error executing task for ${appInfo.packageName}: ${e.message}"
            } finally {
                isLoading = false
            }
            appendConsoleMsg(result)
        }
    }


    // --- UI Structure ---
    Scaffold(
        topBar = {
            TopAppBar(title = { Text("Super Kernel Root Demo") })
        }
    ) { paddingValues ->
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(paddingValues)
                .padding(16.dp)
        ) {
            // Header Text
            Text(
                "Super Kernel Root - Kernel-Level Root Hiding Demo",
                style = MaterialTheme.typography.bodyMedium
            )
            Spacer(Modifier.height(5.dp))
            Text(
                "Next-gen SKRoot: Bypasses all known root detection methods using a completely different approach than Magisk. No SELinux pause required. Full root hiding with high compatibility. Works with all kernels without source code by directly patching the kernel. Compatible with Android apps via JNI. Stable, smooth, and crash-free.",
                style = MaterialTheme.typography.bodySmall
            )
            Spacer(Modifier.height(10.dp))
            HorizontalDivider(thickness = 2.dp, color = Color(0xFFFF00FF))
            Spacer(Modifier.height(10.dp))

            // Main Content Row (Buttons | Output)
            Row(
                modifier = Modifier.fillMaxSize(),
                horizontalArrangement = Arrangement.spacedBy(10.dp)
            ) {
                // Left Column: Buttons
                Column(
                    modifier = Modifier
                        .weight(1f)
                        .verticalScroll(rememberScrollState()),
                    verticalArrangement = Arrangement.spacedBy(5.dp)
                ) {
                    Text("Menu Options:", fontWeight = FontWeight.Bold)
                    Button(modifier = Modifier.fillMaxWidth(), onClick = {
                        runBackgroundTask("Testing Root...") { nativeFunctions.testRoot(rootKey) }
                    }) { Text("1. Test Root Access") }

                    Button(modifier = Modifier.fillMaxWidth(), onClick = { showRootCmdDialog = true }) {
                        Text("2. Execute Root Command")
                    }

                    Button(modifier = Modifier.fillMaxWidth(), onClick = { showInitCmdDialog = true }) {
                        Text("3. Run Kernel Native Command")
                    }

                    Button(modifier = Modifier.fillMaxWidth(), onClick = {
                        runBackgroundTask("Installing SU...") {
                            val insRet = nativeFunctions.installSu(rootKey, suBasePath)
                            if (insRet.contains("installSu done.")) {
                                val suFullPath = nativeFunctions.getLastInstallSuFullPath()
                                withContext(Dispatchers.Main) { // Access context on Main thread
                                    copyToClipboard(suFullPath)
                                    showMsgDialog = "Success" to "SU installed. Path copied:\n$suFullPath"
                                }
                                "$insRet\nlastInstallSuFullPath: $suFullPath" // Return full message
                            } else {
                                insRet // Return installation result only
                            }
                        }
                    }) { Text("4. Install su Environment") }

                    Button(modifier = Modifier.fillMaxWidth(), onClick = {
                        showSuInjectModeDialog = true
                    }) { Text("5. Grant su to Target Process") }

                    Button(modifier = Modifier.fillMaxWidth(), onClick = {
                        runBackgroundTask("Cleaning SU...") { nativeFunctions.uninstallSu(rootKey, suBasePath) }
                    }) { Text("6. Uninstall and Clean su") }

                    Button(modifier = Modifier.fillMaxWidth(), onClick = {
                        showMsgDialog = "Suggestion" to "In order to achieve the best concealment, it is recommended to parasitize on an APP that can stay in the background and is connected to the Internet, such as music, player, sports, radio, and social chat APPs"
                        // After user dismisses the suggestion dialog, show the app selection
                        scope.launch {
                            // A small delay might be needed if the dialog dismissal isn't immediate
                            // kotlinx.coroutines.delay(100)
                            showAppSelectionDialog = AppSelectionConfig(
                                title = "Select App to Inject Into",
                                forceThirtyParty = true, // Restrict selection as per original logic
                                forceRunning = true,
                                onAppSelected = { appInfo ->
                                    runBackgroundTaskWithAppSelection("Pre-checking ${appInfo.showName}...", appInfo) { selectedApp ->
                                        val precheckResult = nativeFunctions.parasitePrecheckApp(rootKey, selectedApp.packageName)
                                        val fileList = parseSoFullPathInfo(precheckResult)

                                        if (fileList.isEmpty() && precheckResult.isNotBlank() && !precheckResult.startsWith("[")) {
                                            // Error during precheck
                                            precheckResult
                                        } else {
                                            withContext(Dispatchers.Main) {
                                                showFileSelectionDialog = FileSelectionConfig(
                                                    title = "Select SO for ${appInfo.showName}",
                                                    fileMap = fileList,
                                                    onFileSelected = { fileInfo ->
                                                        runBackgroundTask("Injecting into ${appInfo.showName}...") {
                                                            val implantResult = nativeFunctions.parasiteImplantApp(
                                                                rootKey,
                                                                appInfo.packageName,
                                                                fileInfo.filePath
                                                            )
                                                            if (implantResult.contains("parasiteImplantApp done.")) {
                                                                withContext(Dispatchers.Main) {
                                                                    showMsgDialog = "Success" to "Successfully parasitized on APP [${appInfo.showName}]"
                                                                }
                                                            }
                                                            implantResult
                                                        }
                                                    }
                                                )
                                            }
                                            precheckResult // Return precheck result to console anyway
                                        }
                                    }
                                }
                            )
                        }
                    }) { Text("7. Inject into Target App") }
                    Button(modifier = Modifier.fillMaxWidth(), onClick = { showRootKeyDialog = true }) {
                        Text("Set Root Key")
                    }
                }

                // Divider
                HorizontalDivider(
                    modifier = Modifier
                        .fillMaxHeight()
                        .width(2.dp),
                    color = Color(0xFFFF00FF)
                )

                // Right Column: Output
                Column(modifier = Modifier.weight(1f)) {
                    Text("Output Info:", fontWeight = FontWeight.Bold)
                    Spacer(Modifier.height(5.dp))
                    Row(horizontalArrangement = Arrangement.spacedBy(5.dp)) {
                        Button(
                            onClick = { copyToClipboard(consoleOutput.text) },
                            modifier = Modifier.weight(1f)
                        ) { Text("Copy") }
                        Button(
                            onClick = { cleanConsoleMsg() },
                            modifier = Modifier.weight(1f)
                        ) { Text("Clear") }
                    }
                    Spacer(Modifier.height(5.dp))
                    SelectionContainer(modifier = Modifier.weight(1f)) { // Make text selectable
                        TextField(
                            value = consoleOutput,
                            onValueChange = { }, // Read-only
                            readOnly = true,
                            modifier = Modifier
                                .fillMaxSize()
                                .weight(1f),
                            textStyle = LocalTextStyle.current.copy(fontSize = 13.sp)
                        )
                    }
                }
            }
        }
    }

    // --- Dialogs ---
    if (isLoading) {
        LoadingDialog(message = loadingMessage)
    }

    if (showRootKeyDialog) {
        InputDialog(
            title = "Enter Root Key",
            initialValue = rootKey,
            onDismiss = { showRootKeyDialog = false },
            onConfirm = { newKey ->
                rootKey = newKey
                showRootKeyDialog = false
                // Optional: Immediately test the new key or just save it
                // runBackgroundTask("Testing new Root Key...") { nativeFunctions.testRoot(rootKey) }
            }
        )
    }

    if (showRootCmdDialog) {
        InputDialog(
            title = "Enter Root Command",
            initialValue = lastInputCmd,
            onDismiss = { showRootCmdDialog = false },
            onConfirm = { cmd ->
                lastInputCmd = cmd
                showRootCmdDialog = false
                runBackgroundTask("Running Root Command...") {
                    "$cmd\n${nativeFunctions.runRootCmd(rootKey, cmd)}"
                }
            }
        )
    }

    if (showInitCmdDialog) {
        InputDialog(
            title = "Enter Kernel Native Command",
            initialValue = lastInputCmd, // Reuse last command state
            onDismiss = { showInitCmdDialog = false },
            onConfirm = { cmd ->
                lastInputCmd = cmd
                showInitCmdDialog = false
                runBackgroundTask("Running Kernel Command...") {
                    "$cmd\n${nativeFunctions.runInit64ProcessCmd(rootKey, cmd)}"
                }
            }
        )
    }

    showMsgDialog?.let { (title, message) ->
        AlertDialog(
            onDismissRequest = { showMsgDialog = null },
            title = { Text(title) },
            text = { Text(message) },
            confirmButton = {
                Button(onClick = { showMsgDialog = null }) {
                    Text("OK")
                }
            }
        )
    }

    if (showSuInjectModeDialog) {
        ListSelectionDialog(
            title = "Select SU Injection Mode",
            items = listOf("Temporary authorization su", "Permanent authorization su"),
            onDismiss = { showSuInjectModeDialog = false },
            onItemSelected = { index ->
                showSuInjectModeDialog = false
                if (index == 0) { // Temporary
                    showAppSelectionDialog = AppSelectionConfig(
                        title = "Select App for Temporary SU",
                        onAppSelected = { appInfo ->
                            runBackgroundTaskWithAppSelection("Injecting Temporary SU into ${appInfo.showName}...", appInfo) { selectedApp ->
                                val injectResult = nativeFunctions.autoSuEnvInject(rootKey, selectedApp.packageName)
                                if(injectResult.contains("autoSuEnvInject done.")) {
                                    withContext(Dispatchers.Main) {
                                        showMsgDialog = "Success" to "ROOT permission has been granted to the APP [${selectedApp.showName}]"
                                    }
                                }
                                injectResult
                            }
                        }
                    )
                } else if (index == 1) { // Permanent
                    showAppSelectionDialog = AppSelectionConfig(
                        title = "Select App for Permanent SU",
                        forceThirtyParty = true, // Restrictions as per original logic
                        forceRunning = true,
                        onAppSelected = { appInfo ->
                            runBackgroundTaskWithAppSelection("Pre-checking ${appInfo.showName} for permanent SU...", appInfo) { selectedApp ->
                                val precheckResult = nativeFunctions.parasitePrecheckApp(rootKey, selectedApp.packageName)
                                val fileList = parseSoFullPathInfo(precheckResult)

                                if (fileList.isEmpty() && precheckResult.isNotBlank() && !precheckResult.startsWith("[")) {
                                    // Error during precheck
                                    precheckResult
                                } else {
                                    withContext(Dispatchers.Main) {
                                        showFileSelectionDialog = FileSelectionConfig(
                                            title = "Select SO for Permanent SU in ${appInfo.showName}",
                                            fileMap = fileList,
                                            onFileSelected = { fileInfo ->
                                                runBackgroundTask("Implanting SU into ${appInfo.showName}...") {
                                                    val implantResult = nativeFunctions.parasiteImplantSuEnv(
                                                        rootKey,
                                                        appInfo.packageName,
                                                        fileInfo.filePath
                                                    )
                                                    if (implantResult.contains("parasiteImplantSuEnv done.")) {
                                                        withContext(Dispatchers.Main) {
                                                            showMsgDialog = "Success" to "The su environment has been permanently parasitized to APP [${appInfo.showName}]"
                                                        }
                                                    }
                                                    implantResult
                                                }
                                            }
                                        )
                                    }
                                    precheckResult // Show precheck result
                                }
                            }
                        }
                    )
                }
            }
        )
    }


    // --- App/File Selection Dialog Logic ---
    showAppSelectionDialog?.let { config ->
        AppSelectionDialog(
            config = config,
            rootKey = rootKey, // Pass necessary data
            nativeGetAllCmdlineProcess = nativeFunctions.getAllCmdlineProcess, // Pass necessary functions
            onDismiss = { showAppSelectionDialog = null }
        )
    }

    showFileSelectionDialog?.let { config ->
        FileSelectionDialog(
            config = config,
            onDismiss = { showFileSelectionDialog = null }
        )
    }
}


// --- Composable Dialogs ---

@Composable
fun LoadingDialog(message: String) {
    Dialog(onDismissRequest = { /* Cannot be dismissed by user */ }) {
        Card(elevation = CardDefaults.cardElevation(defaultElevation = 8.dp), shape = MaterialTheme.shapes.medium) {
            Row(
                modifier = Modifier.padding(16.dp),
                verticalAlignment = Alignment.CenterVertically
            ) {
                CircularProgressIndicator()
                Spacer(modifier = Modifier.width(16.dp))
                Text(message)
            }
        }
    }
}

@Composable
fun InputDialog(
    title: String,
    initialValue: String,
    onDismiss: () -> Unit,
    onConfirm: (String) -> Unit
) {
    var textValue by remember { mutableStateOf(TextFieldValue(initialValue)) }

    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text(title) },
        text = {
            OutlinedTextField(
                value = textValue,
                onValueChange = { textValue = it },
                modifier = Modifier.fillMaxWidth(),
                singleLine = true // Adjust if multiline needed
            )
        },
        confirmButton = {
            Button(onClick = { onConfirm(textValue.text) }) {
                Text("OK")
            }
        },
        dismissButton = {
            Button(onClick = onDismiss) {
                Text("Cancel")
            }
        }
    )
}

@Composable
fun ListSelectionDialog(
    title: String,
    items: List<String>,
    onDismiss: () -> Unit,
    onItemSelected: (Int) -> Unit // Pass index back
) {
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text(title) },
        text = {
            LazyColumn {
                items(items.size) { index ->
                    TextButton(
                        onClick = { onItemSelected(index) },
                        modifier = Modifier.fillMaxWidth()
                    ) {
                        Text(items[index])
                    }
                    if (index < items.size - 1) {
                        HorizontalDivider()
                    }
                }
            }
        },
        confirmButton = {
            Button(onClick = onDismiss) { // Or make confirm do nothing
                Text("Cancel")
            }
        }
    )
}


// --- App/File Selection Dialog Data Structures ---
data class AppSelectionConfig(
    val title: String,
    val onAppSelected: (AppInfo) -> Unit,
    val forceThirtyParty: Boolean = false, // Flags to control initial checkbox state/enabled
    val forceRunning: Boolean = false,
    val allowSystemApps: Boolean = true, // Control if checkbox is enabled
    val allowThirtyPartyApps: Boolean = true,
    val allowRunningApps: Boolean = true
)

data class FileSelectionConfig(
    val title: String,
    val fileMap: Map<String, Int>, // Path -> Status
    val onFileSelected: (FileInfo) -> Unit
)

// --- Composable for App Selection Dialog ---
@Composable
fun AppSelectionDialog(
    config: AppSelectionConfig,
    rootKey: String, // Needed for fetching running apps
    nativeGetAllCmdlineProcess: (String) -> String, // Function dependency
    onDismiss: () -> Unit
) {
    val context = LocalContext.current
    val pm = context.packageManager
    val scope = rememberCoroutineScope()

    var allApps by remember { mutableStateOf<List<AppInfo>>(emptyList()) }
    var runningProcesses by remember { mutableStateOf<Map<Int, String>>(emptyMap()) }
    var filteredApps by remember { mutableStateOf<List<AppInfo>>(emptyList()) }
    var searchText by remember { mutableStateOf("") }
    var showSystemApps by remember { mutableStateOf(!config.forceThirtyParty) } // Initial state based on config
    var showThirdPartyApps by remember { mutableStateOf(true) }
    var showRunningApps by remember { mutableStateOf(config.forceRunning) } // Initial state based on config

    // Fetch app list and running processes once
    LaunchedEffect(Unit) {
        scope.launch(Dispatchers.IO) {
            // Fetch running processes
            val runningAppJson = nativeGetAllCmdlineProcess(rootKey)
            val processes = parseProcessInfo(runningAppJson) // Use your parsing function

            // Fetch installed packages
            val packages = pm.getInstalledPackages(0)
            val appList = packages.mapNotNull { pkgInfo ->
                if (pkgInfo.packageName == context.packageName) return@mapNotNull null // Skip self
                try {
                    val appLabel = pkgInfo.applicationInfo?.loadLabel(pm).toString()
                    val icon = pkgInfo.applicationInfo?.loadIcon(pm)
                    AppInfo(pkgInfo, appLabel, pkgInfo.packageName, icon)
                } catch (e: Exception) {
                    // Handle cases where loading info fails
                    println("Error loading info for ${pkgInfo.packageName}: ${e.message}")
                    null
                }
            }

            withContext(Dispatchers.Main) {
                runningProcesses = processes
                allApps = appList
            }
        }
    }

    // Filter logic - runs whenever dependencies change
    LaunchedEffect(allApps, searchText, showSystemApps, showThirdPartyApps, showRunningApps, runningProcesses) {
        filteredApps = allApps.filter { appInfo ->
            val pkgInfo = appInfo.packageInfo
            val isSystemApp = (pkgInfo.applicationInfo?.flags?.and(ApplicationInfo.FLAG_SYSTEM)) != 0
            val isThirdPartyApp = !isSystemApp
            val isRunning = runningProcesses.values.any { it.contains(appInfo.packageName) }

            // Apply filters
            if (!showSystemApps && isSystemApp) return@filter false
            if (!showThirdPartyApps && isThirdPartyApp) return@filter false
            if (showRunningApps && !isRunning) return@filter false

            // Apply search text
            searchText.isBlank() ||
                    appInfo.packageName.contains(searchText, ignoreCase = true) ||
                    appInfo.showName.contains(searchText, ignoreCase = true)
        }
    }


    Dialog(
        onDismissRequest = onDismiss,
        properties = DialogProperties(usePlatformDefaultWidth = false) // Allow custom width/height
    ) {
        Surface(
            modifier = Modifier
                .fillMaxWidth(0.9f) // 90% of screen width
                .fillMaxHeight(0.85f), // 85% of screen height
            shape = MaterialTheme.shapes.medium,
        ) {
            Column(modifier = Modifier.padding(16.dp)) {
                Text(config.title, style = MaterialTheme.typography.bodyMedium)
                Spacer(Modifier.height(16.dp))

                // Search Bar
                OutlinedTextField(
                    value = searchText,
                    onValueChange = { searchText = it },
                    label = { Text("Search Apps") },
                    modifier = Modifier.fillMaxWidth(),
                    singleLine = true,
                    trailingIcon = {
                        if (searchText.isNotEmpty()) {
                            IconButton(onClick = { searchText = "" }) {
                                Icon(Icons.Filled.Clear, contentDescription = "Clear Search")
                            }
                        }
                    }
                )
                Spacer(Modifier.height(8.dp))

                // Filter Checkboxes
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceEvenly
                ) {
                    CheckboxWithLabel(
                        label = "System",
                        checked = showSystemApps,
                        onCheckedChange = { showSystemApps = it },
                        enabled = config.allowSystemApps && !config.forceThirtyParty // Enable based on config
                    )
                    CheckboxWithLabel(
                        label = "Third-Party",
                        checked = showThirdPartyApps,
                        onCheckedChange = { showThirdPartyApps = it },
                        enabled = config.allowThirtyPartyApps && !config.forceRunning // Enable based on config
                    )
                    CheckboxWithLabel(
                        label = "Running",
                        checked = showRunningApps,
                        onCheckedChange = { showRunningApps = it },
                        enabled = config.allowRunningApps && !config.forceThirtyParty // Enable based on config
                    )
                }
                HorizontalDivider(modifier = Modifier.padding(vertical = 8.dp))

                // App List
                if (allApps.isEmpty()) {
                    Box(modifier = Modifier.weight(1f), contentAlignment = Alignment.Center) {
                        CircularProgressIndicator() // Show loading while apps load
                    }
                } else if (filteredApps.isEmpty()) {
                    Box(modifier = Modifier.weight(1f), contentAlignment = Alignment.Center) {
                        Text("No matching apps found.")
                    }
                }
                else {
                    LazyColumn(modifier = Modifier.weight(1f)) {
                        items(filteredApps, key = { it.packageName }) { appInfo ->
                            AppItemRow(appInfo = appInfo) {
                                config.onAppSelected(appInfo)
                                onDismiss() // Close dialog after selection
                            }
                            HorizontalDivider()
                        }
                    }
                }

                Spacer(Modifier.height(16.dp))
                // Close Button
                Button(onClick = onDismiss, modifier = Modifier.align(Alignment.End)) {
                    Text("Cancel")
                }
            }
        }
    }
}

@Composable
fun CheckboxWithLabel(label: String, checked: Boolean, onCheckedChange: (Boolean) -> Unit, enabled: Boolean = true) {
    Row(verticalAlignment = Alignment.CenterVertically) {
        Checkbox(checked = checked, onCheckedChange = onCheckedChange, enabled = enabled)
        Text(label, modifier = Modifier.padding(start = 4.dp))
    }
}


@Composable
fun AppItemRow(appInfo: AppInfo, onClick: () -> Unit) {
    TextButton(
        onClick = onClick,
        modifier = Modifier.fillMaxWidth()
            .padding(vertical = 8.dp) // Add vertical padding
    ) {
        Row(
            modifier = Modifier.fillMaxWidth(),
            verticalAlignment = Alignment.CenterVertically
        ) {
            appInfo.icon?.let {
                androidx.compose.foundation.Image(
                    bitmap = it.toBitmap(40.dp.value.toInt(), 40.dp.value.toInt()).asImageBitmap(), // Adjust size as needed
                    contentDescription = "${appInfo.showName} icon",
                    modifier = Modifier.size(40.dp).clip(MaterialTheme.shapes.small)
                )
                Spacer(Modifier.width(12.dp))
            }
            Column(modifier = Modifier.weight(1f)) {
                Text(appInfo.showName, fontWeight = FontWeight.Medium)
                Text(appInfo.packageName, style = MaterialTheme.typography.bodyMedium)
            }
        }
    }
}


// --- Composable for File Selection Dialog ---
@Composable
fun FileSelectionDialog(
    config: FileSelectionConfig,
    onDismiss: () -> Unit
) {
    var searchText by remember { mutableStateOf("") }

    // Prepare and sort the list once
    val sortedFileList = remember(config.fileMap) {
        val runningFiles = mutableListOf<FileInfo>()
        val nonRunningFiles = mutableListOf<FileInfo>()

        config.fileMap.forEach { (pathStr, status) ->
            val path = Paths.get(pathStr)
            val fileName = path.fileName.toString()
            val isRecommended = MainActivity.RECOMMEND_FILES.any { it.equals(fileName, ignoreCase = true) }

            when (status) {
                1 -> { // Running
                    val desc = if (isRecommended) "(Recommended, Running)" else "(Running)"
                    runningFiles.add(FileInfo(pathStr, fileName, desc, Color.White))
                }
                2 -> { // Not Running
                    val desc = "(Not Running)"
                    // Original code used Gray, ensure it's visible on your background
                    nonRunningFiles.add(FileInfo(pathStr, fileName, desc, Color.Gray))
                }
                // Add other statuses if needed
            }
        }
        // Prioritize running files, potentially recommended ones first if needed
        runningFiles.sortedByDescending { it.description.contains("Recommended") } + nonRunningFiles
    }

    // Filter based on search text
    val filteredList = remember(sortedFileList, searchText) {
        if (searchText.isBlank()) {
            sortedFileList
        } else {
            sortedFileList.filter { it.fileName.contains(searchText, ignoreCase = true) }
        }
    }


    Dialog(
        onDismissRequest = onDismiss,
        properties = DialogProperties(usePlatformDefaultWidth = false) // Allow custom width/height
    ) {
        Surface(
            modifier = Modifier
                .fillMaxWidth(0.9f) // 90% of screen width
                .fillMaxHeight(0.85f), // 85% of screen height
            shape = MaterialTheme.shapes.medium,
        ) {
            Column(modifier = Modifier.padding(16.dp)) {
                Text(config.title, style = MaterialTheme.typography.bodyMedium)
                Spacer(Modifier.height(16.dp))

                // Search Bar
                OutlinedTextField(
                    value = searchText,
                    onValueChange = { searchText = it },
                    label = { Text("Search SO Files") },
                    modifier = Modifier.fillMaxWidth(),
                    singleLine = true,
                    trailingIcon = {
                        if (searchText.isNotEmpty()) {
                            IconButton(onClick = { searchText = "" }) {
                                Icon(Icons.Filled.Clear, contentDescription = "Clear Search")
                            }
                        }
                    }
                )
                HorizontalDivider(modifier = Modifier.padding(vertical = 8.dp))

                // File List
                if (filteredList.isEmpty()) {
                    Box(modifier = Modifier.weight(1f), contentAlignment = Alignment.Center) {
                        Text(if(sortedFileList.isEmpty()) "No suitable SO files found." else "No matching files found.")
                    }
                } else {
                    LazyColumn(modifier = Modifier.weight(1f)) {
                        items(filteredList, key = { it.filePath }) { fileInfo ->
                            FileItemRow(fileInfo = fileInfo) {
                                config.onFileSelected(fileInfo)
                                onDismiss() // Close dialog after selection
                            }
                            HorizontalDivider()
                        }
                    }
                }

                Spacer(Modifier.height(16.dp))
                // Close Button
                Button(onClick = onDismiss, modifier = Modifier.align(Alignment.End)) {
                    Text("Cancel")
                }
            }
        }
    }
}

@Composable
fun FileItemRow(fileInfo: FileInfo, onClick: () -> Unit) {
    TextButton(
        onClick = onClick,
        modifier = Modifier.fillMaxWidth().padding(vertical = 8.dp)
    ) {
        Row(modifier = Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
            Column(modifier = Modifier.weight(1f)) {
                Text(fileInfo.fileName, fontWeight = FontWeight.Medium, color = fileInfo.textColor)
                Text(fileInfo.description, style = MaterialTheme.typography.bodyMedium, color = fileInfo.textColor)
                // Optionally show full path if needed
                // Text(fileInfo.filePath, style = MaterialTheme.typography.overline, color = fileInfo.textColor)
            }
        }
    }
}


// --- Parsing Functions (Keep these as they are, or make them top-level functions) ---

fun parseProcessInfo(jsonStr: String): Map<Int, String> {
    val processMap = mutableMapOf<Int, String>()
    try {
        if (jsonStr.isBlank() || !jsonStr.startsWith("[")) return emptyMap() // Basic validation
        val jsonArray = JSONArray(jsonStr)
        for (i in 0 until jsonArray.length()) {
            val jsonObject = jsonArray.getJSONObject(i)
            val pid = jsonObject.getInt("pid")
            val encodedValue = jsonObject.getString("name")
            val name = URLDecoder.decode(encodedValue, "UTF-8")
            processMap[pid] = name
        }
    } catch (e: Exception) {
        e.printStackTrace() // Log error
        // Optionally return a map indicating the error?
    }
    return processMap
}

fun parseSoFullPathInfo(jsonStr: String): Map<String, Int> {
    val soPathMap = mutableMapOf<String, Int>()
    try {
        if (jsonStr.isBlank() || !jsonStr.startsWith("[")) return emptyMap() // Basic validation
        val jsonArray = JSONArray(jsonStr)
        for (i in 0 until jsonArray.length()) {
            val jsonObject = jsonArray.getJSONObject(i)
            val encodedValue = jsonObject.getString("name")
            val name = URLDecoder.decode(encodedValue, "UTF-8")
            val status = jsonObject.getInt("status") // Use 'status' key as per your description
            soPathMap[name] = status
        }
    } catch (e: Exception) {
        e.printStackTrace() // Log error
    }
    return soPathMap
}