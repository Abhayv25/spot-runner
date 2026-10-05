# infra/: AWS setup (Phases 7 and 8, YOUR TURN)

This folder is yours. Nothing is pre-made on purpose, so you learn each piece. Do it by hand
in the AWS Console first so you *see* each thing. Then, as a stretch goal, write it all as
code (Terraform) in this folder so anyone can recreate it with one command. "Infrastructure
as code" is a resume line on its own.

## Before anything: protect your wallet

1. Make the account and choose the **Free plan**.
2. Turn on MFA for the root user. Then make a normal IAM user for daily work and stop
   using root.
3. Create a **budget** with an email alert (e.g. $5). Do this first.
4. Pick one region and use it everywhere (e.g. `us-east-1`).
5. End every session by checking: no running EC2 instances you forgot about.

## Phase 7: build the AWS pieces

| Step | What | Learn |
|---|---|---|
| 1 | Install the AWS CLI, run `aws configure` with your IAM user | Credentials, profiles, why keys never go in code |
| 2 | Create an **S3 bucket** for checkpoints | Buckets, keys, private by default, block public access |
| 3 | Create two **SQS queues**: `spot-runner-jobs` and `spot-runner-dead` | Visibility timeout, long polling, dead-letter queues |
| 4 | Install the **AWS SDK for C++** on your Mac (only the `s3` and `sqs` parts, or it takes forever to build) | How C++ libraries get installed and found by CMake |
| 5 | Add `AwsSettings` to `config.h` and write `config/manager.aws.json` | Keeping environments separate |
| 6 | Write `s3_storage.cpp` and `sqs_queue.cpp`, build with `cmake --preset debug-aws` | The SDK pattern: request object, call, check outcome |
| 7 | Run the manager **on your Mac** with `mode: aws`: it uses real S3 and SQS while still using Docker Desktop and the FileWatcher | Swapping one layer at a time |

## Phase 8: real spot computers

| Step | What | Learn |
|---|---|---|
| 8 | Create an **ECR repository** and push your `prime-counter` image | Registries, image tags, `docker login` for ECR |
| 9 | Create an **IAM role** for EC2 that can ONLY: read/write your bucket, use your two queues, pull from your ECR repo | Least privilege (big interview topic) |
| 10 | Launch a small **spot instance** (Amazon Linux or Ubuntu) with that role. Install Docker on it and copy over your manager (or build it there) | EC2, spot pricing, SSH or Session Manager |
| 11 | Handle ECR login on the instance so `ensure_image` can pull. Simplest: run the ECR `docker login` command on the instance; better: add `X-Registry-Auth` support in `docker_client.cpp` | How private registries authenticate |
| 12 | Run the manager with `mode: aws` and the **ImdsWatcher** | Instance metadata, IMDSv2 tokens |
| 13 | Use **AWS Fault Injection Service** to send a real spot interruption to your instance. Watch the job checkpoint, release, and resume on a second instance | Chaos testing, the proof that makes this project |
| 14 | Optional: an **Auto Scaling group** of spot instances that starts the manager on boot (user data script), so a replacement appears by itself | Self-healing systems |
| 15 | Extend `.github/workflows/ci.yml` to build the image and push to ECR on every merge to `main` (use GitHub's OIDC login to AWS, not stored keys) | CI/CD, keyless cloud auth |

## What to measure for the README (and your resume bullet)

- Spot price vs. on-demand price for your instance type, so you can quote real dollars saved.
- Interruptions survived out of interruptions sent (goal: all of them).
- Time from interruption notice → checkpoint uploaded → job resumed elsewhere.
- Work lost per interruption (should be at most one autosave interval).
- Final answer identical with and without interruptions (correctness proof).
