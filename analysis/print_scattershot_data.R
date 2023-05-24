library(ggplot2)
library(dplyr)

args <- commandArgs(trailingOnly = TRUE)
if (nchar(args[1]) == 0)
{
    print("Need to specify scattershot file name (without type extension)")
    stop()
}

# Define the file path and type
file_path <- "C:/Users/Tyler/Documents/repos/sm64_tas_scripting/analysis/"

file_name <- paste0(file_path, args[1])
if (!file.exists(file_name))
{
    print(paste0("'", file_name, "' does not exist."))
    stop()
}

# Optional argument for limiting the number of rows to read from the CSV
# This is necessary if the CSV is still being modified by the brute forcer
df <- data.frame()
if (!is.na(as.numeric(args[2]))) {
    df <- read.csv(file_name, nrows = as.numeric(args[2]))
} else {
    df <- read.csv(file_name)}

print(paste0("CSV rows: ", nrow(df)))

# 
df$x <- df$MarioZ
df$y <- df$MarioX
df$angle <- df$MarioFYaw
df$speed <- df$MarioFSpd
df$shot <- df$Shot
df$frame <- df$Frame

# Define state bin size
x_bins <- seq(range(df$x)[1], range(df$x)[2], by = 10)
y_bins <- seq(range(df$y)[1], range(df$y)[2], by = 10)
angle_bins <- seq(-32768, 32767, by = 8192)
speed_bins <- seq(range(df$speed)[1], range(df$speed)[2], by = 5)

# Create new columns for state bins
df$bin_x <- cut(df$x, breaks = x_bins, include.lowest = TRUE)
df$bin_y <- cut(df$y, breaks = y_bins, include.lowest = TRUE)
df$bin_angle <- cut(df$angle, breaks = angle_bins, include.lowest = TRUE)
df$bin_speed <- cut(df$speed, breaks = speed_bins, include.lowest = TRUE)

# Filter data. Group by + arrange + slice is needed to accurately display scattershot progress over time
# Without it, newer blocks will overwhelm the data
df_top <- df %>%
    #filter(frame > quantile(frame, probs = 0.99) & frame < quantile(frame, probs = 1)) %>%
    filter(abs(PlatNormX) + abs(PlatNormZ) > 0.69) %>%
    #filter(MarioAction == 0x00880456) %>% # Dive Slide
    #filter(speed > 20) %>%
    filter(Sampled == 1) %>%
    filter(Oscillation > 0) %>%
    #filter(Crossing > 2) %>%
    group_by(bin_x, bin_y, bin_angle, bin_speed) %>% 
    arrange(shot) %>% 
    slice(1) %>%
    ungroup()


print(paste0("Rows after filter: ", nrow(df_top)))
print("Plotting...")

# Plot vector field scatterplot
myplot <- ggplot(df_top, aes(x = x, y = y,
    xend = x + cos(angle * pi / 32768), yend = y + sin(angle * pi / 32768), color = shot)) +
    geom_segment(
        aes(
            x = x - speed * cos(angle * pi / 32768) / 2,
            y = y - speed * sin(angle * pi / 32768) / 2,
            alpha = frame),
        arrow = arrow(length = unit(1.5, "pt")),
        linewidth = 0.2) +
    scale_alpha(range = c(0.2, 1)) +
    scale_color_gradient(low = "darkorange", high = "darkgreen") + 
    labs(x = "Mario Z", y = "Mario X", title = "Scattershot Progress")

plot_name <- paste0(file_path, "plot.png")
ggsave(plot_name, myplot)
print("Plot finished.")

# Open the file automatically based on the operating system
if (Sys.info()["sysname"] == "Windows") {
    system(paste0("cmd.exe /c start ", plot_name))
} else if (Sys.info()["sysname"] == "Darwin") {
    system(paste0("open ", plot_name))
} else {
    system(paste0("xdg-open ", plot_name))
}
