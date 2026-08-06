#include <3ds.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <limits>
#include <string>
#include <vector>

namespace {

constexpr int TOP_WIDTH = 400;
constexpr int TOP_HEIGHT = 240;

constexpr int GRID_COLUMNS = 4;
constexpr int GRID_ROWS = 3;
constexpr int PHOTOS_PER_PAGE = GRID_COLUMNS * GRID_ROWS;

constexpr int CELL_WIDTH = 92;
constexpr int CELL_HEIGHT = 66;
constexpr int CELL_GAP_X = 5;
constexpr int CELL_GAP_Y = 7;
constexpr int GRID_START_X = 6;
constexpr int GRID_START_Y = 8;

constexpr int THUMB_WIDTH = 84;
constexpr int THUMB_HEIGHT = 54;

constexpr const char* PHOTO_DIRECTORY =
    "sdmc:/3ds/MaGalerie/photos";

enum class ScreenMode {
    Grid,
    Viewer
};

struct Image {
    int width = 0;
    int height = 0;
    std::vector<u8> rgb;

    bool valid() const {
        return width > 0 && height > 0 && !rgb.empty();
    }
};

u16 readU16(FILE* file) {
    u8 bytes[2]{};

    if (fread(bytes, 1, 2, file) != 2) {
        return 0;
    }

    return static_cast<u16>(
        bytes[0] |
        (static_cast<u16>(bytes[1]) << 8)
    );
}

u32 readU32(FILE* file) {
    u8 bytes[4]{};

    if (fread(bytes, 1, 4, file) != 4) {
        return 0;
    }

    return static_cast<u32>(
        bytes[0] |
        (static_cast<u32>(bytes[1]) << 8) |
        (static_cast<u32>(bytes[2]) << 16) |
        (static_cast<u32>(bytes[3]) << 24)
    );
}

s32 readS32(FILE* file) {
    return static_cast<s32>(readU32(file));
}

bool endsWithBmp(const std::string& filename) {
    if (filename.size() < 4) {
        return false;
    }

    std::string extension =
        filename.substr(filename.size() - 4);

    std::transform(
        extension.begin(),
        extension.end(),
        extension.begin(),
        [](unsigned char character) {
            return static_cast<char>(
                std::tolower(character)
            );
        }
    );

    return extension == ".bmp";
}

std::string baseName(const std::string& path) {
    const std::size_t slash =
        path.find_last_of("/\\");

    if (slash == std::string::npos) {
        return path;
    }

    return path.substr(slash + 1);
}

std::vector<std::string> listPhotos() {
    std::vector<std::string> files;

    DIR* directory = opendir(PHOTO_DIRECTORY);

    if (!directory) {
        return files;
    }

    while (dirent* entry = readdir(directory)) {
        const std::string filename = entry->d_name;

        if (
            filename == "." ||
            filename == ".." ||
            !endsWithBmp(filename)
        ) {
            continue;
        }

        files.push_back(
            std::string(PHOTO_DIRECTORY) +
            "/" +
            filename
        );
    }

    closedir(directory);

    std::sort(files.begin(), files.end());

    return files;
}

bool loadBmp(
    const std::string& path,
    Image& output,
    std::string& error
) {
    FILE* file = fopen(path.c_str(), "rb");

    if (!file) {
        error = "Impossible d'ouvrir la photo.";
        return false;
    }

    const u16 signature = readU16(file);

    if (signature != 0x4D42) {
        fclose(file);
        error = "Le fichier n'est pas un BMP.";
        return false;
    }

    (void)readU32(file);
    (void)readU16(file);
    (void)readU16(file);

    const u32 pixelOffset = readU32(file);
    const u32 dibSize = readU32(file);

    if (dibSize < 40) {
        fclose(file);
        error = "Format BMP non compatible.";
        return false;
    }

    const s32 width = readS32(file);
    const s32 signedHeight = readS32(file);
    const u16 planes = readU16(file);
    const u16 bitsPerPixel = readU16(file);
    const u32 compression = readU32(file);

    if (
        width <= 0 ||
        signedHeight == 0 ||
        planes != 1
    ) {
        fclose(file);
        error = "Dimensions BMP invalides.";
        return false;
    }

    if (
        (bitsPerPixel != 24 &&
         bitsPerPixel != 32) ||
        compression != 0
    ) {
        fclose(file);
        error =
            "BMP 24/32 bits non compresse requis.";
        return false;
    }

    const bool topDown = signedHeight < 0;

    const int height =
        signedHeight < 0
            ? -signedHeight
            : signedHeight;

    if (width > 8192 || height > 8192) {
        fclose(file);
        error = "Cette image est trop grande.";
        return false;
    }

    const int bytesPerPixel =
        bitsPerPixel / 8;

    const std::size_t rowSize =
        (
            static_cast<std::size_t>(width) *
            bitsPerPixel +
            31
        ) /
        32 *
        4;

    std::vector<u8> row(rowSize);

    std::vector<u8> pixels(
        static_cast<std::size_t>(width) *
        height *
        3
    );

    if (
        fseek(
            file,
            static_cast<long>(pixelOffset),
            SEEK_SET
        ) != 0
    ) {
        fclose(file);
        error = "Le fichier BMP est endommage.";
        return false;
    }

    for (
        int sourceRow = 0;
        sourceRow < height;
        ++sourceRow
    ) {
        if (
            fread(
                row.data(),
                1,
                rowSize,
                file
            ) != rowSize
        ) {
            fclose(file);
            error = "Lecture BMP incomplete.";
            return false;
        }

        const int destinationY =
            topDown
                ? sourceRow
                : height - 1 - sourceRow;

        for (int x = 0; x < width; ++x) {
            const std::size_t source =
                static_cast<std::size_t>(x) *
                bytesPerPixel;

            const std::size_t destination =
                (
                    static_cast<std::size_t>(
                        destinationY
                    ) *
                    width +
                    x
                ) *
                3;

            pixels[destination + 0] =
                row[source + 2];

            pixels[destination + 1] =
                row[source + 1];

            pixels[destination + 2] =
                row[source + 0];
        }
    }

    fclose(file);

    output.width = width;
    output.height = height;
    output.rgb = std::move(pixels);

    return true;
}

Image createThumbnail(
    const Image& source,
    int maximumWidth,
    int maximumHeight
) {
    Image thumbnail;

    if (!source.valid()) {
        return thumbnail;
    }

    const float scaleX =
        static_cast<float>(maximumWidth) /
        source.width;

    const float scaleY =
        static_cast<float>(maximumHeight) /
        source.height;

    const float scale =
        std::min(scaleX, scaleY);

    thumbnail.width = std::max(
        1,
        static_cast<int>(
            source.width * scale
        )
    );

    thumbnail.height = std::max(
        1,
        static_cast<int>(
            source.height * scale
        )
    );

    thumbnail.rgb.resize(
        static_cast<std::size_t>(
            thumbnail.width
        ) *
        thumbnail.height *
        3
    );

    for (
        int destinationY = 0;
        destinationY < thumbnail.height;
        ++destinationY
    ) {
        const int sourceY = std::clamp(
            static_cast<int>(
                destinationY / scale
            ),
            0,
            source.height - 1
        );

        for (
            int destinationX = 0;
            destinationX < thumbnail.width;
            ++destinationX
        ) {
            const int sourceX = std::clamp(
                static_cast<int>(
                    destinationX / scale
                ),
                0,
                source.width - 1
            );

            const std::size_t sourceIndex =
                (
                    static_cast<std::size_t>(
                        sourceY
                    ) *
                    source.width +
                    sourceX
                ) *
                3;

            const std::size_t destinationIndex =
                (
                    static_cast<std::size_t>(
                        destinationY
                    ) *
                    thumbnail.width +
                    destinationX
                ) *
                3;

            thumbnail.rgb[
                destinationIndex + 0
            ] = source.rgb[sourceIndex + 0];

            thumbnail.rgb[
                destinationIndex + 1
            ] = source.rgb[sourceIndex + 1];

            thumbnail.rgb[
                destinationIndex + 2
            ] = source.rgb[sourceIndex + 2];
        }
    }

    return thumbnail;
}

void setTopPixel(
    u8* framebuffer,
    int x,
    int y,
    u8 red,
    u8 green,
    u8 blue
) {
    if (
        x < 0 ||
        x >= TOP_WIDTH ||
        y < 0 ||
        y >= TOP_HEIGHT
    ) {
        return;
    }

    const int index =
        3 *
        (
            x * TOP_HEIGHT +
            (TOP_HEIGHT - 1 - y)
        );

    framebuffer[index + 0] = blue;
    framebuffer[index + 1] = green;
    framebuffer[index + 2] = red;
}

void fillRectangle(
    u8* framebuffer,
    int x,
    int y,
    int width,
    int height,
    u8 red,
    u8 green,
    u8 blue
) {
    for (
        int currentX = x;
        currentX < x + width;
        ++currentX
    ) {
        for (
            int currentY = y;
            currentY < y + height;
            ++currentY
        ) {
            setTopPixel(
                framebuffer,
                currentX,
                currentY,
                red,
                green,
                blue
            );
        }
    }
}

void drawRectangleOutline(
    u8* framebuffer,
    int x,
    int y,
    int width,
    int height,
    int thickness,
    u8 red,
    u8 green,
    u8 blue
) {
    fillRectangle(
        framebuffer,
        x,
        y,
        width,
        thickness,
        red,
        green,
        blue
    );

    fillRectangle(
        framebuffer,
        x,
        y + height - thickness,
        width,
        thickness,
        red,
        green,
        blue
    );

    fillRectangle(
        framebuffer,
        x,
        y,
        thickness,
        height,
        red,
        green,
        blue
    );

    fillRectangle(
        framebuffer,
        x + width - thickness,
        y,
        thickness,
        height,
        red,
        green,
        blue
    );
}

void clearTop(
    u8 red = 5,
    u8 green = 7,
    u8 blue = 11
) {
    u16 framebufferWidth = 0;
    u16 framebufferHeight = 0;

    u8* framebuffer = gfxGetFramebuffer(
        GFX_TOP,
        GFX_LEFT,
        &framebufferWidth,
        &framebufferHeight
    );

    (void)framebufferWidth;
    (void)framebufferHeight;

    fillRectangle(
        framebuffer,
        0,
        0,
        TOP_WIDTH,
        TOP_HEIGHT,
        red,
        green,
        blue
    );
}

void drawImageAt(
    u8* framebuffer,
    const Image& image,
    int x,
    int y
) {
    if (!image.valid()) {
        return;
    }

    for (
        int sourceY = 0;
        sourceY < image.height;
        ++sourceY
    ) {
        for (
            int sourceX = 0;
            sourceX < image.width;
            ++sourceX
        ) {
            const std::size_t pixel =
                (
                    static_cast<std::size_t>(
                        sourceY
                    ) *
                    image.width +
                    sourceX
                ) *
                3;

            setTopPixel(
                framebuffer,
                x + sourceX,
                y + sourceY,
                image.rgb[pixel + 0],
                image.rgb[pixel + 1],
                image.rgb[pixel + 2]
            );
        }
    }
}

void drawViewerImage(
    const Image& image,
    float zoom
) {
    clearTop();

    if (!image.valid()) {
        return;
    }

    const float fitX =
        static_cast<float>(TOP_WIDTH) /
        image.width;

    const float fitY =
        static_cast<float>(TOP_HEIGHT) /
        image.height;

    const float scale =
        std::min(fitX, fitY) * zoom;

    const int drawWidth = std::max(
        1,
        static_cast<int>(
            image.width * scale
        )
    );

    const int drawHeight = std::max(
        1,
        static_cast<int>(
            image.height * scale
        )
    );

    const int startX =
        (TOP_WIDTH - drawWidth) / 2;

    const int startY =
        (TOP_HEIGHT - drawHeight) / 2;

    u16 framebufferWidth = 0;
    u16 framebufferHeight = 0;

    u8* framebuffer = gfxGetFramebuffer(
        GFX_TOP,
        GFX_LEFT,
        &framebufferWidth,
        &framebufferHeight
    );

    (void)framebufferWidth;
    (void)framebufferHeight;

    for (
        int destinationY = 0;
        destinationY < drawHeight;
        ++destinationY
    ) {
        const int screenY =
            startY + destinationY;

        if (
            screenY < 0 ||
            screenY >= TOP_HEIGHT
        ) {
            continue;
        }

        const int sourceY = std::clamp(
            static_cast<int>(
                destinationY / scale
            ),
            0,
            image.height - 1
        );

        for (
            int destinationX = 0;
            destinationX < drawWidth;
            ++destinationX
        ) {
            const int screenX =
                startX + destinationX;

            if (
                screenX < 0 ||
                screenX >= TOP_WIDTH
            ) {
                continue;
            }

            const int sourceX = std::clamp(
                static_cast<int>(
                    destinationX / scale
                ),
                0,
                image.width - 1
            );

            const std::size_t pixel =
                (
                    static_cast<std::size_t>(
                        sourceY
                    ) *
                    image.width +
                    sourceX
                ) *
                3;

            setTopPixel(
                framebuffer,
                screenX,
                screenY,
                image.rgb[pixel + 0],
                image.rgb[pixel + 1],
                image.rgb[pixel + 2]
            );
        }
    }
}

void buildThumbnailPage(
    const std::vector<std::string>& files,
    std::size_t page,
    std::vector<Image>& thumbnails,
    std::string& status
) {
    thumbnails.clear();

    const std::size_t pageStart =
        page * PHOTOS_PER_PAGE;

    const std::size_t pageEnd =
        std::min(
            pageStart + PHOTOS_PER_PAGE,
            files.size()
        );

    for (
        std::size_t index = pageStart;
        index < pageEnd;
        ++index
    ) {
        Image image;
        std::string error;

        if (loadBmp(files[index], image, error)) {
            thumbnails.push_back(
                createThumbnail(
                    image,
                    THUMB_WIDTH,
                    THUMB_HEIGHT
                )
            );
        } else {
            thumbnails.push_back(Image{});
            status = error;
        }
    }
}

void drawGrid(
    const std::vector<std::string>& files,
    const std::vector<Image>& thumbnails,
    std::size_t selectedIndex,
    std::size_t page
) {
    clearTop(4, 6, 10);

    u16 framebufferWidth = 0;
    u16 framebufferHeight = 0;

    u8* framebuffer = gfxGetFramebuffer(
        GFX_TOP,
        GFX_LEFT,
        &framebufferWidth,
        &framebufferHeight
    );

    (void)framebufferWidth;
    (void)framebufferHeight;

    const std::size_t pageStart =
        page * PHOTOS_PER_PAGE;

    for (
        int localIndex = 0;
        localIndex < PHOTOS_PER_PAGE;
        ++localIndex
    ) {
        const int column =
            localIndex % GRID_COLUMNS;

        const int row =
            localIndex / GRID_COLUMNS;

        const int cellX =
            GRID_START_X +
            column *
            (CELL_WIDTH + CELL_GAP_X);

        const int cellY =
            GRID_START_Y +
            row *
            (CELL_HEIGHT + CELL_GAP_Y);

        const std::size_t globalIndex =
            pageStart +
            static_cast<std::size_t>(
                localIndex
            );

        fillRectangle(
            framebuffer,
            cellX,
            cellY,
            CELL_WIDTH,
            CELL_HEIGHT,
            15,
            19,
            27
        );

        if (globalIndex >= files.size()) {
            continue;
        }

        const bool selected =
            globalIndex == selectedIndex;

        if (selected) {
            drawRectangleOutline(
                framebuffer,
                cellX,
                cellY,
                CELL_WIDTH,
                CELL_HEIGHT,
                3,
                58,
                185,
                255
            );
        } else {
            drawRectangleOutline(
                framebuffer,
                cellX,
                cellY,
                CELL_WIDTH,
                CELL_HEIGHT,
                1,
                45,
                52,
                66
            );
        }

        const std::size_t thumbnailIndex =
            globalIndex - pageStart;

        if (
            thumbnailIndex <
                thumbnails.size() &&
            thumbnails[
                thumbnailIndex
            ].valid()
        ) {
            const Image& thumbnail =
                thumbnails[thumbnailIndex];

            const int imageX =
                cellX +
                (
                    CELL_WIDTH -
                    thumbnail.width
                ) /
                2;

            const int imageY =
                cellY +
                (
                    CELL_HEIGHT -
                    thumbnail.height
                ) /
                2;

            drawImageAt(
                framebuffer,
                thumbnail,
                imageX,
                imageY
            );
        }
    }

    if (!files.empty()) {
        const int progressWidth =
            static_cast<int>(
                (
                    static_cast<double>(
                        selectedIndex + 1
                    ) /
                    files.size()
                ) *
                TOP_WIDTH
            );

        fillRectangle(
            framebuffer,
            0,
            TOP_HEIGHT - 3,
            TOP_WIDTH,
            3,
            20,
            25,
            34
        );

        fillRectangle(
            framebuffer,
            0,
            TOP_HEIGHT - 3,
            progressWidth,
            3,
            58,
            185,
            255
        );
    }
}

void printGridInformation(
    const std::vector<std::string>& files,
    std::size_t selectedIndex,
    const std::string& status
) {
    consoleClear();

    printf(
        "\x1b[1;2H"
        "\x1b[36mMaGalerie 3DS"
        "\x1b[0m"
    );

    printf(
        "\x1b[3;2H"
        "GALERIE"
    );

    if (files.empty()) {
        printf(
            "\x1b[5;2H"
            "Aucune photo BMP trouvee."
        );

        printf(
            "\x1b[7;2H"
            "/3ds/MaGalerie/photos/"
        );

        printf(
            "\x1b[10;2H"
            "X : actualiser"
        );
    } else {
        printf(
            "\x1b[5;2H"
            "Photo %lu sur %lu",
            static_cast<unsigned long>(
                selectedIndex + 1
            ),
            static_cast<unsigned long>(
                files.size()
            )
        );

        printf(
            "\x1b[7;2H"
            "%.34s",
            baseName(
                files[selectedIndex]
            ).c_str()
        );

        printf(
            "\x1b[11;2H"
            "Croix : selectionner"
        );

        printf(
            "\x1b[12;2H"
            "A      : ouvrir"
        );

        printf(
            "\x1b[13;2H"
            "L / R  : changer de page"
        );

        printf(
            "\x1b[14;2H"
            "X      : actualiser"
        );
    }

    printf(
        "\x1b[16;2H"
        "START  : quitter"
    );

    if (!status.empty()) {
        printf(
            "\x1b[19;2H"
            "\x1b[31m%.34s"
            "\x1b[0m",
            status.c_str()
        );
    }
}

void printViewerInformation(
    const std::vector<std::string>& files,
    std::size_t selectedIndex,
    float zoom,
    const std::string& status
) {
    consoleClear();

    printf(
        "\x1b[1;2H"
        "\x1b[36mMaGalerie 3DS"
        "\x1b[0m"
    );

    printf(
        "\x1b[3;2H"
        "APERÇU"
    );

    if (!files.empty()) {
        printf(
            "\x1b[5;2H"
            "Photo %lu sur %lu",
            static_cast<unsigned long>(
                selectedIndex + 1
            ),
            static_cast<unsigned long>(
                files.size()
            )
        );

        printf(
            "\x1b[7;2H"
            "%.34s",
            baseName(
                files[selectedIndex]
            ).c_str()
        );

        printf(
            "\x1b[9;2H"
            "Zoom : %.2fx",
            zoom
        );
    }

    printf(
        "\x1b[12;2H"
        "Gauche / Droite : changer"
    );

    printf(
        "\x1b[13;2H"
        "Haut / Bas      : zoom"
    );

    printf(
        "\x1b[14;2H"
        "B               : galerie"
    );

    printf(
        "\x1b[16;2H"
        "START           : quitter"
    );

    if (!status.empty()) {
        printf(
            "\x1b[19;2H"
            "\x1b[31m%.34s"
            "\x1b[0m",
            status.c_str()
        );
    }
}

bool loadViewerPhoto(
    const std::vector<std::string>& files,
    std::size_t selectedIndex,
    Image& viewerImage,
    std::string& status
) {
    viewerImage = Image{};
    status.clear();

    if (files.empty()) {
        return false;
    }

    return loadBmp(
        files[selectedIndex],
        viewerImage,
        status
    );
}

} // namespace

int main() {
    gfxInitDefault();
    consoleInit(GFX_BOTTOM, nullptr);

    std::vector<std::string> files =
        listPhotos();

    std::size_t selectedIndex = 0;
    std::size_t currentPage = 0;

    std::size_t cachedThumbnailPage =
        std::numeric_limits<std::size_t>::max();

    std::vector<Image> thumbnails;

    Image viewerImage;

    ScreenMode mode = ScreenMode::Grid;

    float zoom = 1.0f;

    std::string status;

    bool needsThumbnailReload = true;
    bool needsViewerLoad = false;
    bool needsRedraw = true;

    while (aptMainLoop()) {
        hidScanInput();

        const u32 pressed = hidKeysDown();

        if (pressed & KEY_START) {
            break;
        }

        if (pressed & KEY_X) {
            files = listPhotos();

            selectedIndex = 0;
            currentPage = 0;
            cachedThumbnailPage =
                std::numeric_limits<
                    std::size_t
                >::max();

            thumbnails.clear();
            viewerImage = Image{};
            zoom = 1.0f;
            status.clear();

            mode = ScreenMode::Grid;

            needsThumbnailReload = true;
            needsViewerLoad = false;
            needsRedraw = true;
        }

        if (mode == ScreenMode::Grid) {
            if (!files.empty()) {
                const std::size_t oldIndex =
                    selectedIndex;

                if (
                    pressed & KEY_LEFT &&
                    selectedIndex %
                        GRID_COLUMNS >
                        0
                ) {
                    --selectedIndex;
                }

                if (
                    pressed & KEY_RIGHT &&
                    selectedIndex + 1 <
                        files.size() &&
                    selectedIndex %
                        GRID_COLUMNS <
                        GRID_COLUMNS - 1
                ) {
                    ++selectedIndex;
                }

                if (
                    pressed & KEY_UP &&
                    selectedIndex >=
                        GRID_COLUMNS
                ) {
                    selectedIndex -=
                        GRID_COLUMNS;
                }

                if (
                    pressed & KEY_DOWN &&
                    selectedIndex +
                        GRID_COLUMNS <
                        files.size()
                ) {
                    selectedIndex +=
                        GRID_COLUMNS;
                }

                if (pressed & KEY_L) {
                    if (
                        selectedIndex >=
                        PHOTOS_PER_PAGE
                    ) {
                        selectedIndex -=
                            PHOTOS_PER_PAGE;
                    } else {
                        selectedIndex = 0;
                    }
                }

                if (pressed & KEY_R) {
                    selectedIndex = std::min(
                        selectedIndex +
                            PHOTOS_PER_PAGE,
                        files.size() - 1
                    );
                }

                if (selectedIndex != oldIndex) {
                    const std::size_t newPage =
                        selectedIndex /
                        PHOTOS_PER_PAGE;

                    if (newPage != currentPage) {
                        currentPage = newPage;
                        needsThumbnailReload =
                            true;
                    }

                    needsRedraw = true;
                }

                if (pressed & KEY_A) {
                    mode = ScreenMode::Viewer;
                    zoom = 1.0f;
                    needsViewerLoad = true;
                    needsRedraw = true;
                }
            }
        } else {
            if (pressed & KEY_B) {
                mode = ScreenMode::Grid;
                zoom = 1.0f;
                needsRedraw = true;
            }

            if (!files.empty()) {
                if (pressed & KEY_RIGHT) {
                    selectedIndex =
                        (
                            selectedIndex + 1
                        ) %
                        files.size();

                    zoom = 1.0f;
                    needsViewerLoad = true;
                }

                if (pressed & KEY_LEFT) {
                    selectedIndex =
                        (
                            selectedIndex +
                            files.size() -
                            1
                        ) %
                        files.size();

                    zoom = 1.0f;
                    needsViewerLoad = true;
                }

                if (pressed & KEY_UP) {
                    zoom = std::min(
                        4.0f,
                        zoom + 0.25f
                    );

                    needsRedraw = true;
                }

                if (pressed & KEY_DOWN) {
                    zoom = std::max(
                        0.50f,
                        zoom - 0.25f
                    );

                    needsRedraw = true;
                }
            }
        }

        if (
            mode == ScreenMode::Grid &&
            needsThumbnailReload
        ) {
            buildThumbnailPage(
                files,
                currentPage,
                thumbnails,
                status
            );

            cachedThumbnailPage =
                currentPage;

            (void)cachedThumbnailPage;

            needsThumbnailReload = false;
            needsRedraw = true;
        }

        if (
            mode == ScreenMode::Viewer &&
            needsViewerLoad
        ) {
            loadViewerPhoto(
                files,
                selectedIndex,
                viewerImage,
                status
            );

            needsViewerLoad = false;
            needsRedraw = true;
        }

        if (needsRedraw) {
            if (mode == ScreenMode::Grid) {
                drawGrid(
                    files,
                    thumbnails,
                    selectedIndex,
                    currentPage
                );

                printGridInformation(
                    files,
                    selectedIndex,
                    status
                );
            } else {
                drawViewerImage(
                    viewerImage,
                    zoom
                );

                printViewerInformation(
                    files,
                    selectedIndex,
                    zoom,
                    status
                );
            }

            gfxFlushBuffers();
            gfxSwapBuffers();
            gspWaitForVBlank();

            needsRedraw = false;
        } else {
            gspWaitForVBlank();
        }
    }

    gfxExit();

    return 0;
}