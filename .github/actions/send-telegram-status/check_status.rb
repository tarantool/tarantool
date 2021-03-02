require 'octokit'
require 'json'
require 'optparse'
require 'colorize'

options = OpenStruct.new
options.tnt_path = '.'
options.branch = 'master'
options.verbosity = 0
options.com_tag = 0
options.com_back = 0
options.file_issues = "issues.txt"
options.no_color = false

OptionParser.new do |opt|
  opt.on("-s", "--source <TARANTOOL SOURCE PATH, default '#{options.tnt_path}'>") { |s| options.tnt_path = s }
  opt.on("-b", "--branch <BRANCH, ex. 1.10>") { |b| options.branch = b }
  opt.on("-t", "--tag <COMMIT TAG long format> (disables --commits_back option)") { |t| options.com_tag = t }
  opt.on("--commits_back <number of commits like for BRANCH~2, ex. 2>") { |c| options.com_back = c }
  opt.on("-v", "--verbosity <NUMBER, [0-3]>") { |v| options.verbosity = v.to_i }
  opt.on("-f", "--file_issues <FILE NAME, default '#{options.file_issues}'>") { |f| options.file_issues = f }
  opt.on("--no-color>") { options.no_color = true }
end.parse!

if options.com_tag != 0
  git_sha = options.com_tag
else
  # remove EOL by strip
  git_sha = %x( cd #{options.tnt_path} && git rev-parse origin/#{options.branch}~#{options.com_back} ).strip
end
if options.verbosity > 0
  puts "Options: #{options}"
  puts "In branch #{options.branch} search for GIT SHA: #{git_sha}"
end

client = Octokit::Client.new :access_token => ENV['GITHUB_PATOKEN']

# returns the same as client.last_response.data
client.repository_workflow_runs('tarantool/tarantool', {event: 'push', branch: options.branch})

# point and start work from the initial response
last_response = client.last_response

# print found results pages
if options.verbosity > 1
  number_of_pages = last_response.rels[:last].href.match(/page=(\d+).*$/)[1]
  puts "There are #{last_response.data.total_count} results, on #{number_of_pages} pages!"
end

file = File.open(options.file_issues, 'w')

# print in loop all paginated pages
loop do
  # for verbosity print all the current page data
  if options.verbosity > 2
    puts last_response.data.map(&:to_s).to_json
  end

  # print from workflow array needed item
  workflow_array = last_response.data[:workflow_runs]
  workflow_array.each do |item|
    if item[:head_sha] == git_sha
      if item[:conclusion] == 'success'
        if options.no_color
          puts "#{item[:conclusion]} #{item[:name]}"
        else
          puts "#{item[:conclusion].green} #{item[:name]}"
        end
      else
        if options.no_color
          issue = "#{item[:conclusion]} #{item[:name]}"
        else
          issue = "#{item[:conclusion].red} #{item[:name]}"
        end
        file.write("#{issue}\n")
        puts issue
      end
    end
  end

  if last_response.rels[:next].nil?
    break
  end

  # iterate and get next page data
  last_response = last_response.rels[:next].get
end

file.close()
