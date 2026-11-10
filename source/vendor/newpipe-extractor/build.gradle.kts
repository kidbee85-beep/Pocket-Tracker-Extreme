// Frozen upstream sources; see PROVENANCE.json and UPSTREAM-libs.versions.toml.
plugins { `java-library` }

java {
    sourceCompatibility = JavaVersion.VERSION_17
    targetCompatibility = JavaVersion.VERSION_17
}
tasks.withType<JavaCompile>().configureEach {
    options.encoding = "UTF-8"
    options.release.set(17)
}
sourceSets.main {
    java.setSrcDirs(listOf("extractor/src/main/java", "timeago-parser/src/main/java", "nanojson/src/main/java", "generated"))
    resources.srcDir("extractor/src/main/resources")
}
dependencies {
    api("org.jsoup:jsoup:1.23.2")
    api("com.google.code.findbugs:jsr305:3.0.2")
    api("com.google.protobuf:protobuf-javalite:4.36.2")
    api("org.mozilla:rhino:1.8.1")
    api("org.mozilla:rhino-engine:1.8.1")
}
