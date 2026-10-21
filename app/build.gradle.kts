import java.security.MessageDigest
import java.util.Properties

plugins {
    id("com.android.application")
}

val localProperties = Properties().apply {
    val file = rootProject.file("local.properties")
    if (file.isFile) file.inputStream().use { load(it) }
}

// Release signing: keystore.properties (not tracked) names a key kept outside
// the repository. Without it, release builds are left unsigned.
val keystoreProperties = Properties().apply {
    val file = rootProject.file("keystore.properties")
    if (file.isFile) file.inputStream().use { load(it) }
}

// The Android edition has the Windows edition's version number: ORMEQ_VER in
// the game's src/defines.h. The version code follows it (1.2.3 is 10203).
val gameVersion: MatchResult = Regex("""#define\s+ORMEQ_VER\s+"(\d+)\.(\d+)\.(\d+)"""")
    .find(rootProject.file("game/src/defines.h").readText())
    ?: throw GradleException("No ORMEQ_VER in game/src/defines.h")
val (versionMajor, versionMinor, versionPatch) = gameVersion.destructured.toList().map { it.toInt() }
require(versionMinor < 100 && versionPatch < 100) { "ORMEQ_VER is beyond the version code's scheme" }

// The game's artwork (the character plates and tile sheets, with the
// questgiver portraits), sound effects and music are licensed separately (see
// game/LICENSE) and are not in this repository. Official builds name a folder
// holding them, laid out as they install (lib/art/, lib/tiles/pack/,
// lib/xtra/sound/pack/, lib/xtra/music/pack/), as ormeq.assetPack in
// local.properties or with -Pormeq.assetPack=...; without one, the game draws
// glyphs, uses its synthesised sound effects and plays no music.
val assetPack: File? = ((project.findProperty("ormeq.assetPack") as String?)
    ?: localProperties.getProperty("ormeq.assetPack"))
    ?.let { rootProject.file(it) }
    ?.also { require(it.isDirectory) { "ormeq.assetPack is not a folder: $it" } }

android {
    namespace = "com.ormeq.game"
    compileSdk = 36
    ndkVersion = "30.0.16248370"

    defaultConfig {
        applicationId = "com.ormeq.game"
        minSdk = 26
        targetSdk = 36
        versionCode = versionMajor * 10000 + versionMinor * 100 + versionPatch
        versionName = "$versionMajor.$versionMinor.$versionPatch"

        ndk {
            // -Pormeq.abis=x86_64 builds only what the emulator needs.
            val abis = (project.findProperty("ormeq.abis") as String?)?.split(",") ?: listOf("arm64-v8a", "x86_64")
            abiFilters += abis
        }
    }

    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = "3.31.6"
        }
    }

    sourceSets {
        getByName("main") {
            // SDL's Java glue must match the SDL version being built, so it is used in place.
            java.directories.add("../third_party/SDL/android-project/app/src/main/java")
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    signingConfigs {
        if (keystoreProperties.containsKey("storeFile")) {
            create("release") {
                storeFile = file(keystoreProperties.getProperty("storeFile"))
                storePassword = keystoreProperties.getProperty("storePassword")
                keyAlias = keystoreProperties.getProperty("keyAlias")
                keyPassword = keystoreProperties.getProperty("keyPassword")
            }
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = false
            signingConfig = signingConfigs.findByName("release")
            // The APK then depends only on the source, not on the checkout's Git history.
            vcsInfo.include = false
        }
    }

    // No dependency report for Google Play in the signing block: it is
    // encrypted afresh on every build, and without it the same source (and
    // asset pack) gives the same APK, byte for byte.
    dependenciesInfo {
        includeInApk = false
        includeInBundle = false
    }

    lint {
        abortOnError = false
    }
}

/**
 * Lays out the game's runtime files as APK assets, with the index that
 * android-main.c reads to install them on first run: every file that
 * game/runtime-files.txt lists, from game/, and every file in the asset pack
 * when there is one.
 */
abstract class StageGameAssets : DefaultTask() {
    @get:InputDirectory
    abstract val gameDir: DirectoryProperty

    @get:Optional
    @get:InputDirectory
    abstract val assetPackDir: DirectoryProperty

    @get:OutputDirectory
    abstract val outputDir: DirectoryProperty

    @TaskAction
    fun stage() {
        val game = gameDir.get().asFile
        val pack = assetPackDir.orNull?.asFile
        val out = outputDir.get().asFile
        out.deleteRecursively()
        out.mkdirs()

        val files = sortedMapOf<String, File>()
        for (raw in game.resolve("runtime-files.txt").readLines()) {
            val name = raw.trim()
            if (name.isEmpty() || name.startsWith("#")) continue
            files[name] = game.resolve(name).also { require(it.isFile) { "Missing game file: $name" } }
        }
        var packed = 0
        pack?.walkTopDown()?.filter { it.isFile }?.forEach { file ->
            val name = file.relativeTo(pack).invariantSeparatorsPath
            require(name.startsWith("lib/")) { "The asset pack holds a file outside lib/: $name" }
            require(name !in files) { "The asset pack repeats a game file: $name" }
            files[name] = file
            packed++
        }

        val digest = MessageDigest.getInstance("SHA-256")
        val index = StringBuilder()
        for ((name, file) in files) {
            val bytes = file.readBytes()
            out.resolve(name).apply { parentFile.mkdirs() }.writeBytes(bytes)
            digest.update(name.toByteArray())
            digest.update(MessageDigest.getInstance("SHA-256").digest(bytes))
            index.append(bytes.size).append(' ').append(name).append('\n')
        }
        val stamp = digest.digest().joinToString("") { "%02x".format(it) }.take(16)
        out.resolve("ormeq-assets.txt").writeText("ormeq-assets 1\nstamp $stamp\n$index")
        logger.lifecycle("Staged ${files.size} game files ($packed from the asset pack), stamp $stamp")
    }
}

val stageGameAssets = tasks.register<StageGameAssets>("stageGameAssets") {
    gameDir.set(rootProject.layout.projectDirectory.dir("game"))
    assetPack?.let { assetPackDir.set(it) }
    outputDir.set(layout.buildDirectory.dir("generated/gameAssets"))
}

androidComponents {
    onVariants { variant ->
        variant.sources.assets?.addGeneratedSourceDirectory(stageGameAssets, StageGameAssets::outputDir)
    }
}
