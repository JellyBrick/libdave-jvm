plugins {
    id("java-test-fixtures")
}

dependencies {
    api(projects.api)

    compileOnly(libs.jetbrains.annotations)
    compileOnly(libs.jda)

    testFixturesImplementation(platform(libs.junit.bom))
    testFixturesImplementation(libs.junit.jupiter)
    testFixturesApi(libs.netty.buffer)
}

mavenPublishing {
    pom {
        name = "adapter-jda"
    }
}
