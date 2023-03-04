hours = 1
categories = ["Info", "Debug", "Critical"]
sources = (1..10).map { |sourceIndex| "Source #{sourceIndex}"}

open('example.log', 'w') do |f|
  hours.times.each do |hour| 
    60.times.each do |minute|
      60.times.each do |second|
        1000.times.each do |millisecond|
          categories.each do |category|
            f << "[15.02.2023 #{hour.to_s.rjust(2, "0")}:#{minute.to_s.rjust(2, "0")}:#{second.to_s.rjust(2, "0")}.#{millisecond.to_s.rjust(3, "0")}][#{category}][#{sources.sample}] #{category} message\n"
          end
        end
      end
    end
  end
end
