# Why
This is a small example of why I believe that all software should work nearly instantaneously on current year computers.

# What
We have a log file of the following format:
```
[15.02.2023 00:00:00.000][Info] Info message
[15.02.2023 00:00:00.000][Debug] Debug message
[15.02.2023 00:00:00.000][Critical] Critical message
```
We need to filter it by timestamp, log category(Info, Debug or Critical), text contents and display the result.
  
We load the entire 522MB log file into RAM in a single read. Next, we parse the file data for searching by converting date-time values to integers and log categories to flags and storing pointers to original file data and never copying anything. This approach to parsing is called in-place parsing. Reading the whole file and parsing takes just one second on the first execution and 0.4 seconds on subsequent executions. Additionally, filtering operations scan the entire data set and complete in approximately 50 milliseconds.

# Video demo

[![Demo](https://img.youtube.com/vi/0mu0B4OE_IE/mqdefault.jpg)](https://www.youtube.com/watch?v=0mu0B4OE_IE)


# How to create a log example file
```
sudo apt install ruby  
ruby log_file_generator.rb
```