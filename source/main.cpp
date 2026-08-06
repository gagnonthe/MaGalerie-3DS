#include <3ds.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <string>
#include <vector>

namespace {
constexpr int SCREEN_W = 400;
constexpr int SCREEN_H = 240;
constexpr const char* PHOTO_DIR = "sdmc:/3ds/MaGalerie/photos";

struct Image {
    int width = 0;
    int height = 0;
    std::vector<u8> rgb; // RGB, ligne par ligne depuis le haut
};

u16 readU16(FILE* f) {
    u8 b[2]{};
    if (fread(b, 1, 2, f) != 2) return 0;
    return static_cast<u16>(b[0] | (b[1] << 8));
}

u32 readU32(FILE* f) {
    u8 b[4]{};
    if (fread(b, 1, 4, f) != 4) return 0;
    return static_cast<u32>(b[0] | (b[1] << 8) | (b[2] << 16) | (b[3] << 24));
}

s32 readS32(FILE* f) {
    return static_cast<s32>(readU32(f));
}

bool endsWithBmp(const std::string& name) {
    if (name.size() < 4) return false;
    std::string ext = name.substr(name.size() - 4);
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return ext == ".bmp";
}

std::vector<std::string> listPhotos() {
    std::vector<std::string> files;
    DIR* dir = opendir(PHOTO_DIR);
    if (!dir) return files;

    while (dirent* entry = readdir(dir)) {
        const std::string name = entry->d_name;
        if (name == "." || name == ".." || !endsWithBmp(name)) continue;
        files.push_back(std::string(PHOTO_DIR) + "/" + name);
    }
    closedir(dir);

    std::sort(files.begin(), files.end());
    return files;
}

std::string baseName(const std::string& path) {
    const std::size_t pos = path.find_last_of("/\\");
    return pos == std::string::npos ? path : path.substr(pos + 1);
}

bool loadBmp(const std::string& path, Image& out, std::string& error) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) {
        error = "Impossible d'ouvrir le fichier";
        return false;
    }

    const u16 signature = readU16(f);
    if (signature != 0x4D42) {
        fclose(f);
        error = "Ce fichier n'est pas un BMP";
        return false;
    }

    (void)readU32(f); // taille du fichier
    (void)readU16(f);
    (void)readU16(f);
    const u32 pixelOffset = readU32(f);
    const u32 dibSize = readU32(f);
    if (dibSize < 40) {
        fclose(f);
        error = "Format BMP trop ancien";
        return false;
    }

    const s32 width = readS32(f);
    const s32 signedHeight = readS32(f);
    const u16 planes = readU16(f);
    const u16 bitsPerPixel = readU16(f);
    const u32 compression = readU32(f);

    if (width <= 0 || signedHeight == 0 || planes != 1) {
        fclose(f);
        error = "Dimensions BMP invalides";
        return false;
    }
    if ((bitsPerPixel != 24 && bitsPerPixel != 32) || compression != 0) {
        fclose(f);
        error = "Utilise un BMP 24 ou 32 bits non compresse";
        return false;
    }

    const bool topDown = signedHeight < 0;
    const int height = signedHeight < 0 ? -signedHeight : signedHeight;
    const int bytesPerPixel = bitsPerPixel / 8;
    const std::size_t rowSize = ((static_cast<std::size_t>(width) * bitsPerPixel + 31) / 32) * 4;

    if (width > 8192 || height > 8192) {
        fclose(f);
        error = "Image trop grande";
        return false;
    }

    std::vector<u8> row(rowSize);
    std::vector<u8> rgb(static_cast<std::size_t>(width) * height * 3);

    if (fseek(f, static_cast<long>(pixelOffset), SEEK_SET) != 0) {
        fclose(f);
        error = "BMP endommage";
        return false;
    }

    for (int sourceRow = 0; sourceRow < height; ++sourceRow) {
        if (fread(row.data(), 1, rowSize, f) != rowSize) {
            fclose(f);
            error = "Lecture BMP incomplete";
            return false;
        }

        const int y = topDown ? sourceRow : (height - 1 - sourceRow);
        for (int x = 0; x < width; ++x) {
            const std::size_t src = static_cast<std::size_t>(x) * bytesPerPixel;
            const std::size_t dst = (static_cast<std::size_t>(y) * width + x) * 3;
            rgb[dst + 0] = row[src + 2];
            rgb[dst + 1] = row[src + 1];
            rgb[dst + 2] = row[src + 0];
        }
    }

    fclose(f);
    out.width = width;
    out.height = height;
    out.rgb = std::move(rgb);
    return true;
}

void setPixel(u8* fb, int x, int y, u8 r, u8 g, u8 b) {
    if (x < 0 || x >= SCREEN_W || y < 0 || y >= SCREEN_H) return;
    // L'ecran 3DS est stocke tourne, en BGR8.
    const int index = 3 * (x * SCREEN_H + (SCREEN_H - 1 - y));
    fb[index + 0] = b;
    fb[index + 1] = g;
    fb[index + 2] = r;
}

void clearTop(u8 r = 0, u8 g = 0, u8 b = 0) {
    u16 fbWidth = 0;
    u16 fbHeight = 0;
    u8* fb = gfxGetFramebuffer(GFX_TOP, GFX_LEFT, &fbWidth, &fbHeight);
    (void)fbWidth;
    (void)fbHeight;
    for (int x = 0; x < SCREEN_W; ++x) {
        for (int y = 0; y < SCREEN_H; ++y) {
            setPixel(fb, x, y, r, g, b);
        }
    }
}

void drawImage(const Image& image, float zoom) {
    clearTop(12, 12, 16);
    if (image.width <= 0 || image.height <= 0 || image.rgb.empty()) return;

    const float fitX = static_cast<float>(SCREEN_W) / image.width;
    const float fitY = static_cast<float>(SCREEN_H) / image.height;
    const float scale = std::min(fitX, fitY) * zoom;

    int drawW = std::max(1, static_cast<int>(image.width * scale));
    int drawH = std::max(1, static_cast<int>(image.height * scale));
    const int startX = (SCREEN_W - drawW) / 2;
    const int startY = (SCREEN_H - drawH) / 2;

    u16 fbWidth = 0;
    u16 fbHeight = 0;
    u8* fb = gfxGetFramebuffer(GFX_TOP, GFX_LEFT, &fbWidth, &fbHeight);
    (void)fbWidth;
    (void)fbHeight;

    for (int dy = 0; dy < drawH; ++dy) {
        const int screenY = startY + dy;
        if (screenY < 0 || screenY >= SCREEN_H) continue;
        const int srcY = std::clamp(static_cast<int>(dy / scale), 0, image.height - 1);

        for (int dx = 0; dx < drawW; ++dx) {
            const int screenX = startX + dx;
            if (screenX < 0 || screenX >= SCREEN_W) continue;
            const int srcX = std::clamp(static_cast<int>(dx / scale), 0, image.width - 1);
            const std::size_t p = (static_cast<std::size_t>(srcY) * image.width + srcX) * 3;
            setPixel(fb, screenX, screenY, image.rgb[p], image.rgb[p + 1], image.rgb[p + 2]);
        }
    }
}

void printBottom(const std::vector<std::string>& files, std::size_t index,
                 const std::string& status, float zoom) {
    consoleClear();
    printf("\x1b[1;1HMaGalerie 3DS\n");
    printf("------------------------------\n");

    if (files.empty()) {
        printf("Aucune photo BMP trouvee.\n\n");
        printf("Cree ce dossier avec FTPD :\n");
        printf("/3ds/MaGalerie/photos/\n\n");
        printf("Puis ajoute des images .bmp\n");
        printf("en 24 ou 32 bits.\n");
    } else {
        printf("Photo %lu / %lu\n", static_cast<unsigned long>(index + 1),
               static_cast<unsigned long>(files.size()));
        printf("%s\n", baseName(files[index]).c_str());
        printf("Zoom : %.1fx\n", zoom);
        if (!status.empty()) printf("\n%s\n", status.c_str());
    }

    printf("\nGauche/Droite : changer\n");
    printf("Haut/Bas       : zoom\n");
    printf("X              : recharger\n");
    printf("START          : quitter\n");
}
}

int main() {
    gfxInitDefault();
    consoleInit(GFX_BOTTOM, nullptr);

    std::vector<std::string> files = listPhotos();
    std::size_t index = 0;
    Image image;
    std::string status;
    float zoom = 1.0f;
    bool needsLoad = !files.empty();
    bool needsDraw = true;

    printBottom(files, index, status, zoom);

    while (aptMainLoop()) {
        hidScanInput();
        const u32 down = hidKeysDown();

        if (down & KEY_START) break;

        if (down & KEY_X) {
            files = listPhotos();
            index = 0;
            zoom = 1.0f;
            status.clear();
            image = Image{};
            needsLoad = !files.empty();
            needsDraw = true;
        }

        if (!files.empty()) {
            if (down & KEY_RIGHT) {
                index = (index + 1) % files.size();
                zoom = 1.0f;
                needsLoad = true;
            }
            if (down & KEY_LEFT) {
                index = (index + files.size() - 1) % files.size();
                zoom = 1.0f;
                needsLoad = true;
            }
            if (down & KEY_UP) {
                zoom = std::min(4.0f, zoom + 0.25f);
                needsDraw = true;
            }
            if (down & KEY_DOWN) {
                zoom = std::max(0.25f, zoom - 0.25f);
                needsDraw = true;
            }
        }

        if (needsLoad) {
            status.clear();
            Image loaded;
            if (loadBmp(files[index], loaded, status)) {
                image = std::move(loaded);
            } else {
                image = Image{};
            }
            needsLoad = false;
            needsDraw = true;
        }

            if (needsDraw) {
            drawImage(image, zoom);
            printBottom(files, index, status, zoom);

            gfxFlushBuffers();
            gfxSwapBuffers();
            gspWaitForVBlank();

            needsDraw = false;
        } else {
            // Attendre la prochaine image sans changer inutilement
            // de framebuffer, ce qui évite le clignotement.
            gspWaitForVBlank();
        }

    gfxExit();
    return 0;
}
